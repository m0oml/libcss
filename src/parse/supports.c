/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2026 Trevor Cockayne
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "stylesheet.h"
#include "parse/custom.h"
#include "parse/language.h"
#include "parse/propstrings.h"
#include "parse/supports.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"
#include "utils/utils.h"

static css_error supports_condition(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, bool *result);

/**
 * Skip to the end of a bracketed block, whose opening has been read.
 *
 * \return true if the end was found
 */
static bool supports_skip_block(const parserutils_vector *vector,
		int32_t *ctx)
{
	const css_token *token;
	int depth = 1;

	while ((token = parserutils_vector_iterate(vector, ctx)) != NULL) {
		if (token->type == CSS_TOKEN_FUNCTION || tokenIsChar(token, '('))
			depth++;
		else if (tokenIsChar(token, ')') && --depth == 0)
			return true;
	}

	return false;
}

/**
 * Check whether a declaration is supported, as the value parses.
 *
 * \param c         Parsing context
 * \param property  The property name
 * \param vector    Vector of tokens, at the value
 * \param ctx       Pointer to vector iteration context, updated to the
 *                  closing bracket, or the end
 * \return true if supported
 */
static bool supports_declaration(css_language *c, const css_token *property,
		const parserutils_vector *vector, int32_t *ctx)
{
	parserutils_vector *value = NULL;
	css_style *style = NULL;
	const css_token *token;
	css_prop_handler handler;
	int32_t value_ctx = 0;
	bool supported = false;
	int depth = 0;
	int i;

	if (css__is_custom_property_name(property->idata))
		return true;

	for (i = FIRST_PROP; i <= LAST_PROP; i++) {
		bool match = false;

		if (lwc_string_caseless_isequal(property->idata, c->strings[i],
				&match) == lwc_error_ok && match)
			break;
	}
	if (i == LAST_PROP + 1)
		return false;

	/* Copy the value up to the closing bracket, so the property's
	 * parser sees only the value */
	if (parserutils_vector_create(sizeof(css_token), 16, &value) !=
			PARSERUTILS_OK)
		return false;

	while ((token = parserutils_vector_peek(vector, *ctx)) != NULL) {
		if (depth == 0 && tokenIsChar(token, ')'))
			break;

		if (token->type == CSS_TOKEN_FUNCTION || tokenIsChar(token, '('))
			depth++;
		else if (tokenIsChar(token, ')'))
			depth--;

		if (parserutils_vector_append(value, (void *) token) !=
				PARSERUTILS_OK) {
			parserutils_vector_destroy(value);
			return false;
		}
		parserutils_vector_iterate(vector, ctx);
	}

	if (css__value_uses_var(value, 0)) {
		parserutils_vector_destroy(value);
		return true;
	}

	handler = property_handlers[i - FIRST_PROP];
	if (css__stylesheet_style_create(c->sheet, &style) != CSS_OK) {
		parserutils_vector_destroy(value);
		return false;
	}

	if (handler(c, value, &value_ctx, style) == CSS_OK) {
		consumeWhitespace(value, &value_ctx);
		supported = parserutils_vector_peek(value, value_ctx) == NULL;
	}

	css__stylesheet_style_destroy(style);
	parserutils_vector_destroy(value);
	return supported;
}

/**
 * Parse a supports-in-parens: a bracketed condition or declaration, or a
 * function, which is not supported.
 */
static css_error supports_in_parens(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, bool *result)
{
	const css_token *token;
	css_error error;

	token = parserutils_vector_iterate(vector, ctx);
	if (token == NULL)
		return CSS_INVALID;

	if (token->type == CSS_TOKEN_FUNCTION) {
		/* selector() and the like */
		*result = false;
		return supports_skip_block(vector, ctx) ? CSS_OK : CSS_INVALID;
	}

	if (tokenIsChar(token, '(') == false)
		return CSS_INVALID;

	consumeWhitespace(vector, ctx);
	token = parserutils_vector_peek(vector, *ctx);

	if (token != NULL && token->type == CSS_TOKEN_IDENT) {
		int32_t colon_ctx = *ctx + 1;

		consumeWhitespace(vector, &colon_ctx);
		if (tokenIsChar(parserutils_vector_peek(vector, colon_ctx),
				':')) {
			const css_token *property = token;

			colon_ctx++;
			consumeWhitespace(vector, &colon_ctx);
			*result = supports_declaration(c, property, vector,
					&colon_ctx);
			*ctx = colon_ctx;
			return supports_skip_block(vector, ctx) ?
					CSS_OK : CSS_INVALID;
		}
	}

	if (token != NULL && (tokenIsChar(token, '(') ||
			token->type == CSS_TOKEN_FUNCTION ||
			token->type == CSS_TOKEN_IDENT)) {
		int32_t cond_ctx = *ctx;

		error = supports_condition(c, vector, &cond_ctx, result);
		if (error == CSS_OK) {
			consumeWhitespace(vector, &cond_ctx);
			if (tokenIsChar(parserutils_vector_iterate(vector,
					&cond_ctx), ')')) {
				*ctx = cond_ctx;
				return CSS_OK;
			}
		}
	}

	/* Anything else is not supported */
	*result = false;
	return supports_skip_block(vector, ctx) ? CSS_OK : CSS_INVALID;
}

/**
 * Check if a token is the keyword given
 */
static bool supports_is_keyword(css_language *c, const css_token *token,
		int string)
{
	bool match;

	return token != NULL && token->type == CSS_TOKEN_IDENT &&
			lwc_string_caseless_isequal(token->idata,
			c->strings[string], &match) == lwc_error_ok && match;
}

/**
 * Parse a supports condition: not, or terms joined by and or by or.
 */
static css_error supports_condition(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, bool *result)
{
	const css_token *token;
	css_error error;
	int joiner = -1;

	token = parserutils_vector_peek(vector, *ctx);
	if (supports_is_keyword(c, token, NOT)) {
		parserutils_vector_iterate(vector, ctx);
		consumeWhitespace(vector, ctx);
		error = supports_in_parens(c, vector, ctx, result);
		*result = !*result;
		return error;
	}

	error = supports_in_parens(c, vector, ctx, result);
	if (error != CSS_OK)
		return error;

	while (true) {
		int32_t next_ctx = *ctx;
		bool term;
		int keyword;

		consumeWhitespace(vector, &next_ctx);
		token = parserutils_vector_peek(vector, next_ctx);
		if (supports_is_keyword(c, token, AND))
			keyword = AND;
		else if (supports_is_keyword(c, token, OR))
			keyword = OR;
		else
			return CSS_OK;

		/* and and or may not be mixed without brackets */
		if (joiner != -1 && joiner != keyword)
			return CSS_INVALID;
		joiner = keyword;

		parserutils_vector_iterate(vector, &next_ctx);
		consumeWhitespace(vector, &next_ctx);
		error = supports_in_parens(c, vector, &next_ctx, &term);
		if (error != CSS_OK)
			return error;

		*result = keyword == AND ? (*result && term) : (*result || term);
		*ctx = next_ctx;
	}
}

/* Internally exported function, documented in src/parse/supports.h */
css_error css__parse_supports(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, bool *result)
{
	css_error error;

	*result = false;

	consumeWhitespace(vector, ctx);
	error = supports_condition(c, vector, ctx, result);
	if (error != CSS_OK)
		return error;

	consumeWhitespace(vector, ctx);
	if (parserutils_vector_peek(vector, *ctx) != NULL)
		return CSS_INVALID;

	return CSS_OK;
}

static css_error supports_resolve(void *pw, const char *base,
		lwc_string *rel, lwc_string **abs)
{
	UNUSED(pw);
	UNUSED(base);

	*abs = lwc_string_ref(rel);
	return CSS_OK;
}

/* Exported function, documented in include/libcss/stylesheet.h */
css_error css_supports(const char *condition, size_t len, bool *supported)
{
	static const char prefix[] = "@supports ";
	static const char suffix[] = " { a { color: red } }";
	css_stylesheet_params params;
	css_stylesheet *sheet;
	css_error error;
	char *text;

	if (condition == NULL || supported == NULL)
		return CSS_BADPARM;

	*supported = false;

	text = malloc(sizeof(prefix) + len + sizeof(suffix));
	if (text == NULL)
		return CSS_NOMEM;

	memcpy(text, prefix, sizeof(prefix) - 1);
	memcpy(text + sizeof(prefix) - 1, condition, len);
	memcpy(text + sizeof(prefix) - 1 + len, suffix, sizeof(suffix));

	memset(&params, 0, sizeof(params));
	params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
	params.level = CSS_LEVEL_DEFAULT;
	params.charset = "UTF-8";
	params.url = "supports:";
	params.title = NULL;
	params.resolve = supports_resolve;

	error = css_stylesheet_create(&params, &sheet);
	if (error != CSS_OK) {
		free(text);
		return error;
	}

	error = css_stylesheet_append_data(sheet, (const uint8_t *) text,
			sizeof(prefix) - 1 + len + sizeof(suffix) - 1);
	if (error == CSS_NEEDDATA)
		error = css_stylesheet_data_done(sheet);

	/* The condition holds if the sheet has a media rule which matches */
	if (error == CSS_OK || error == CSS_IMPORTS_PENDING) {
		const css_rule *rule = sheet->rule_list;

		if (rule != NULL && rule->type == CSS_RULE_MEDIA) {
			const css_mq_query *media =
				((const css_rule_media *) rule)->media;

			*supported = media != NULL && media->type != 0;
		}
		error = CSS_OK;
	}

	css_stylesheet_destroy(sheet);
	free(text);

	return error;
}
