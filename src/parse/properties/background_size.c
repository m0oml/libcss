/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2026 Trevor Cockayne
 */

#include <assert.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/**
 * Parse one dimension of background-size
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx     Pointer to vector iteration context
 * \param set     Updated to true if a length was given, false for auto
 * \param length  Updated to the length
 * \param unit    Updated to the length's unit
 * \return CSS_OK on success, CSS_INVALID if there is no dimension
 */
static css_error parse_size_dimension(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		bool *set, css_fixed *length, uint32_t *unit)
{
	int32_t orig_ctx = *ctx;
	const css_token *token;
	css_error error;
	bool match;

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	if (token->type == CSS_TOKEN_IDENT &&
			lwc_string_caseless_isequal(token->idata,
					c->strings[AUTO], &match) ==
					lwc_error_ok && match) {
		parserutils_vector_iterate(vector, ctx);
		*set = false;
		return CSS_OK;
	}

	if (token->type != CSS_TOKEN_DIMENSION &&
			token->type != CSS_TOKEN_NUMBER &&
			token->type != CSS_TOKEN_PERCENTAGE)
		return CSS_INVALID;

	error = css__parse_unit_specifier(c, vector, ctx, UNIT_PX,
			length, unit);
	if (error != CSS_OK) {
		*ctx = orig_ctx;
		return error;
	}

	/* Only non-negative lengths and percentages are allowed, and a
	 * number only if it is zero */
	if ((*unit & (UNIT_LENGTH | UNIT_PCT)) == 0 || *length < 0 ||
			(token->type == CSS_TOKEN_NUMBER && *length != 0)) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}

	*set = true;
	return CSS_OK;
}

/**
 * Parse background-size
 *
 * \param c	  Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx	  Pointer to vector iteration context
 * \param result  Pointer to location to receive resulting style
 * \return CSS_OK on success,
 *	   CSS_NOMEM on memory exhaustion,
 *	   CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *		   If the input is invalid, then \a *ctx remains unchanged.
 */
css_error css__parse_background_size(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	int32_t orig_ctx = *ctx;
	const css_token *token;
	enum flag_value flag_value;
	uint16_t value = BACKGROUND_SIZE_SET;
	bool set[2] = { false, false };
	css_fixed length[2] = { 0, 0 };
	uint32_t unit[2] = { UNIT_PX, UNIT_PX };
	css_error error;
	bool match;
	int i;

	/* cover | contain | [ length | percentage | auto ]{1,2} */
	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	flag_value = get_css_flag_value(c, token);
	if (flag_value != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		return css_stylesheet_style_flag_value(result, flag_value,
				CSS_PROP_BACKGROUND_SIZE);
	}

	if (token->type == CSS_TOKEN_IDENT &&
			lwc_string_caseless_isequal(token->idata,
					c->strings[COVER], &match) ==
					lwc_error_ok && match) {
		parserutils_vector_iterate(vector, ctx);
		return css__stylesheet_style_appendOPV(result,
				CSS_PROP_BACKGROUND_SIZE, 0,
				BACKGROUND_SIZE_COVER);
	}

	if (token->type == CSS_TOKEN_IDENT &&
			lwc_string_caseless_isequal(token->idata,
					c->strings[CONTAIN], &match) ==
					lwc_error_ok && match) {
		parserutils_vector_iterate(vector, ctx);
		return css__stylesheet_style_appendOPV(result,
				CSS_PROP_BACKGROUND_SIZE, 0,
				BACKGROUND_SIZE_CONTAIN);
	}

	error = parse_size_dimension(c, vector, ctx,
			&set[0], &length[0], &unit[0]);
	if (error != CSS_OK) {
		*ctx = orig_ctx;
		return error;
	}

	/* An optional height, which is auto if omitted */
	consumeWhitespace(vector, ctx);
	if (parserutils_vector_peek(vector, *ctx) != NULL) {
		int32_t height_ctx = *ctx;

		if (parse_size_dimension(c, vector, ctx, &set[1],
				&length[1], &unit[1]) != CSS_OK)
			*ctx = height_ctx;
	}

	if (set[0])
		value |= BACKGROUND_SIZE_WIDTH_SET;
	if (set[1])
		value |= BACKGROUND_SIZE_HEIGHT_SET;

	error = css__stylesheet_style_appendOPV(result,
			CSS_PROP_BACKGROUND_SIZE, 0, value);
	if (error != CSS_OK) {
		*ctx = orig_ctx;
		return error;
	}

	for (i = 0; i < 2; i++) {
		if (set[i] == false)
			continue;

		error = css__stylesheet_style_vappend(result, 2,
				length[i], unit[i]);
		if (error != CSS_OK) {
			*ctx = orig_ctx;
			return error;
		}
	}

	return CSS_OK;
}
