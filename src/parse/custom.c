/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2026 Trevor Cockayne
 */

/** \file
 * Parsing of custom properties and of declarations which use var().
 *
 * Neither can be turned into normal bytecode when the stylesheet is
 * parsed, so the text of their values is stored instead.  The values of
 * declarations using var() are parsed during selection, once the custom
 * properties they refer to are known.
 */

#include <assert.h>
#include <string.h>
#include <strings.h>

#include <parserutils/utils/buffer.h>

#include "bytecode/bytecode.h"
#include "parse/custom.h"
#include "stylesheet.h"
#include "utils/parserutilserror.h"
#include "utils/utils.h"

/* Exported function, documented in parse/custom.h */
bool css__is_custom_property_name(lwc_string *name)
{
	return lwc_string_length(name) > 2 &&
			lwc_string_data(name)[0] == '-' &&
			lwc_string_data(name)[1] == '-';
}

/* Exported function, documented in parse/custom.h */
bool css__value_uses_var(const parserutils_vector *vector, int32_t ctx)
{
	const css_token *token;

	while ((token = parserutils_vector_iterate(vector, &ctx)) != NULL) {
		if (token->type == CSS_TOKEN_FUNCTION &&
				lwc_string_length(token->idata) == 3 &&
				strncasecmp(lwc_string_data(token->idata),
						"var", 3) == 0) {
			return true;
		}
	}

	return false;
}

/**
 * Append a string to a buffer
 */
static css_error append(parserutils_buffer *buf, const char *data, size_t len)
{
	return css_error_from_parserutils_error(
			parserutils_buffer_append(buf,
					(const uint8_t *) data, len));
}

/**
 * Append a quoted string to a buffer
 */
static css_error append_quoted(parserutils_buffer *buf, lwc_string *str)
{
	const char *data = lwc_string_data(str);
	size_t len = lwc_string_length(str);
	css_error error;
	size_t i;

	error = append(buf, "\"", 1);
	for (i = 0; error == CSS_OK && i < len; i++) {
		if (data[i] == '"' || data[i] == '\\') {
			error = append(buf, "\\", 1);
			if (error != CSS_OK)
				break;
			error = append(buf, &data[i], 1);
		} else if (data[i] == '\n') {
			error = append(buf, "\\a ", 3);
		} else {
			error = append(buf, &data[i], 1);
		}
	}
	if (error == CSS_OK)
		error = append(buf, "\"", 1);

	return error;
}

/**
 * Append the text of a token to a buffer
 */
static css_error append_token(parserutils_buffer *buf, const css_token *token)
{
	const char *prefix = NULL;
	const char *suffix = NULL;
	css_error error;

	switch (token->type) {
	case CSS_TOKEN_STRING:
	case CSS_TOKEN_INVALID_STRING:
		return append_quoted(buf, token->idata);
	case CSS_TOKEN_URI:
		error = append(buf, "url(", 4);
		if (error == CSS_OK)
			error = append_quoted(buf, token->idata);
		if (error == CSS_OK)
			error = append(buf, ")", 1);
		return error;
	case CSS_TOKEN_ATKEYWORD:
		prefix = "@";
		break;
	case CSS_TOKEN_HASH:
		prefix = "#";
		break;
	case CSS_TOKEN_UNICODE_RANGE:
		prefix = "U+";
		break;
	case CSS_TOKEN_FUNCTION:
		suffix = "(";
		break;
	case CSS_TOKEN_PERCENTAGE:
		suffix = "%";
		break;
	case CSS_TOKEN_IDENT:
	case CSS_TOKEN_CHAR:
	case CSS_TOKEN_NUMBER:
	case CSS_TOKEN_DIMENSION:
		break;
	case CSS_TOKEN_CDO:
		return append(buf, "<!--", 4);
	case CSS_TOKEN_CDC:
		return append(buf, "-->", 3);
	case CSS_TOKEN_S:
		return append(buf, " ", 1);
	case CSS_TOKEN_INCLUDES:
		return append(buf, "~=", 2);
	case CSS_TOKEN_DASHMATCH:
		return append(buf, "|=", 2);
	case CSS_TOKEN_PREFIXMATCH:
		return append(buf, "^=", 2);
	case CSS_TOKEN_SUFFIXMATCH:
		return append(buf, "$=", 2);
	case CSS_TOKEN_SUBSTRINGMATCH:
		return append(buf, "*=", 2);
	default:
		return CSS_OK;
	}

	if (token->idata == NULL)
		return CSS_INVALID;

	error = CSS_OK;
	if (prefix != NULL)
		error = append(buf, prefix, strlen(prefix));
	if (error == CSS_OK)
		error = append(buf, lwc_string_data(token->idata),
				lwc_string_length(token->idata));
	if (error == CSS_OK && suffix != NULL)
		error = append(buf, suffix, strlen(suffix));

	return error;
}

/**
 * Get a token from a vector
 */
static inline const css_token *token_at(const parserutils_vector *vector,
		int32_t i)
{
	return parserutils_vector_peek(vector, i);
}

/**
 * Get the text of a declaration's value, without any !important
 *
 * \param vector     Vector of tokens to process
 * \param ctx        Pointer to vector iteration context
 * \param result     Updated to the value text, which may be empty
 * \param important  Updated to whether the declaration is !important
 * \return CSS_OK on success, appropriate error otherwise
 *
 * Post condition: \a *ctx is past the end of the declaration
 */
static css_error serialise_value(const parserutils_vector *vector,
		int32_t *ctx, lwc_string **result, bool *important)
{
	parserutils_buffer *buf = NULL;
	const css_token *token;
	int32_t first = *ctx;
	int32_t end;
	int32_t i;
	css_error error = CSS_OK;

	*important = false;

	/* Find the end of the value */
	while (parserutils_vector_iterate(vector, ctx) != NULL)
		;
	end = *ctx;

	/* Remove trailing whitespace and any "! S* important" */
	while (end > first && token_at(vector, end - 1)->type ==
			CSS_TOKEN_S)
		end--;
	token = end > first ? token_at(vector, end - 1) : NULL;
	if (token != NULL && token->type == CSS_TOKEN_IDENT &&
			lwc_string_length(token->idata) == 9 &&
			strncasecmp(lwc_string_data(token->idata),
					"important", 9) == 0) {
		int32_t bang = end - 1;

		while (bang > first && token_at(vector, bang - 1)->type ==
				CSS_TOKEN_S)
			bang--;
		if (bang > first &&
				tokenIsChar(token_at(vector, bang - 1), '!')) {
			*important = true;
			end = bang - 1;
			while (end > first && token_at(vector, end - 1)->type ==
					CSS_TOKEN_S)
				end--;
		}
	}

	/* Skip leading whitespace */
	while (first < end && token_at(vector, first)->type ==
			CSS_TOKEN_S)
		first++;

	if (parserutils_buffer_create(&buf) != PARSERUTILS_OK)
		return CSS_NOMEM;

	for (i = first; i < end && error == CSS_OK; i++) {
		token = token_at(vector, i);
		if (token->type == CSS_TOKEN_S && i > first &&
				token_at(vector, i - 1)->type ==
						CSS_TOKEN_S)
			continue;
		error = append_token(buf, token);
	}

	if (error == CSS_OK) {
		error = css_error_from_lwc_error(lwc_intern_string(
				(const char *) buf->data, buf->length,
				result));
	}

	parserutils_buffer_destroy(buf);

	return error;
}

/* Exported function, documented in parse/custom.h */
css_error css__parse_custom_property(css_language *c, lwc_string *name,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result, bool *important)
{
	lwc_string *value = NULL;
	uint32_t name_idx;
	uint32_t value_idx;
	css_error error;

	error = serialise_value(vector, ctx, &value, important);
	if (error != CSS_OK)
		return error;

	/* The string table takes ownership of the references */
	error = css__stylesheet_string_add(c->sheet, lwc_string_ref(name),
			&name_idx);
	if (error != CSS_OK) {
		lwc_string_unref(value);
		return error;
	}

	error = css__stylesheet_string_add(c->sheet, value, &value_idx);
	if (error != CSS_OK)
		return error;

	return css__stylesheet_style_vappend(result, 3,
			buildOPV(OP_CUSTOM_PROPERTY, 0, 0),
			name_idx, value_idx);
}

/**
 * Find the properties set by a property or shorthand
 *
 * Shorthands set all of their properties when given "initial", which
 * gives the list.
 *
 * \param c        Parsing context
 * \param handler  Parser for the property
 * \param props    Updated to style with one opcode per property set
 * \return CSS_OK on success, appropriate error otherwise
 */
static css_error properties_set_by(css_language *c, css_prop_handler handler,
		css_style **props)
{
	parserutils_vector *vector;
	css_token initial;
	css_style *style = NULL;
	int32_t ctx = 0;
	css_error error;

	if (parserutils_vector_create(sizeof(css_token), 1, &vector) !=
			PARSERUTILS_OK)
		return CSS_NOMEM;

	memset(&initial, 0, sizeof(initial));
	initial.type = CSS_TOKEN_IDENT;
	initial.idata = c->strings[INITIAL];
	initial.data.data = (uint8_t *) lwc_string_data(initial.idata);
	initial.data.len = lwc_string_length(initial.idata);

	if (parserutils_vector_append(vector, &initial) != PARSERUTILS_OK) {
		parserutils_vector_destroy(vector);
		return CSS_NOMEM;
	}

	error = css__stylesheet_style_create(c->sheet, &style);
	if (error == CSS_OK)
		error = handler(c, vector, &ctx, style);

	parserutils_vector_destroy(vector);

	if (error != CSS_OK) {
		if (style != NULL)
			css__stylesheet_style_destroy(style);
		return error;
	}

	*props = style;
	return CSS_OK;
}

/* Exported function, documented in parse/custom.h */
css_error css__parse_var_declaration(css_language *c,
		css_prop_handler handler, lwc_string *name,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result, bool *important)
{
	css_style *props = NULL;
	lwc_string *value = NULL;
	uint32_t name_idx;
	uint32_t value_idx;
	uint32_t i;
	css_error error;

	error = properties_set_by(c, handler, &props);
	if (error != CSS_OK)
		return error;

	error = serialise_value(vector, ctx, &value, important);
	if (error != CSS_OK)
		goto cleanup;

	/* The string table takes ownership of the references */
	error = css__stylesheet_string_add(c->sheet, lwc_string_ref(name),
			&name_idx);
	if (error != CSS_OK) {
		lwc_string_unref(value);
		goto cleanup;
	}

	error = css__stylesheet_string_add(c->sheet, value, &value_idx);
	if (error != CSS_OK)
		goto cleanup;

	for (i = 0; i < props->used; i++) {
		css_code_t opv = props->bytecode[i];

		/* "initial" sets each property with no further data */
		if (getFlagValue(opv) != FLAG_VALUE_INITIAL) {
			error = CSS_INVALID;
			break;
		}

		error = css__stylesheet_style_vappend(result, 3,
				buildOPV(getOpcode(opv), FLAG_VAR, 0),
				value_idx, name_idx);
		if (error != CSS_OK)
			break;
	}

cleanup:
	css__stylesheet_style_destroy(props);
	return error;
}
