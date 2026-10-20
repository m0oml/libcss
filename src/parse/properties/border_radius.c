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

/** The corners, in the order the shorthand gives them */
static const enum css_properties_e corner_prop[4] = {
	CSS_PROP_BORDER_TOP_LEFT_RADIUS,
	CSS_PROP_BORDER_TOP_RIGHT_RADIUS,
	CSS_PROP_BORDER_BOTTOM_RIGHT_RADIUS,
	CSS_PROP_BORDER_BOTTOM_LEFT_RADIUS
};

/**
 * Parse one radius, a non-negative length or percentage
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx     Pointer to vector iteration context
 * \param length  Updated to the length
 * \param unit    Updated to the length's unit
 * \return CSS_OK on success, CSS_INVALID if there is no radius
 */
static css_error parse_radius(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_fixed *length, uint32_t *unit)
{
	int32_t orig_ctx = *ctx;
	const css_token *token;
	css_error error;

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

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

	/* A number is only allowed if it is zero */
	if ((*unit & (UNIT_LENGTH | UNIT_PCT)) == 0 || *length < 0 ||
			(token->type == CSS_TOKEN_NUMBER && *length != 0)) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}

	return CSS_OK;
}

/**
 * Add the style of a corner
 */
static css_error append_corner(css_style *result, enum css_properties_e prop,
		css_fixed hlength, uint32_t hunit,
		css_fixed vlength, uint32_t vunit)
{
	css_error error;

	error = css__stylesheet_style_appendOPV(result, prop, 0,
			BORDER_RADIUS_SET);
	if (error != CSS_OK)
		return error;

	return css__stylesheet_style_vappend(result, 4,
			hlength, hunit, vlength, vunit);
}

/**
 * Parse a border-*-radius property
 *
 * \param c	  Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx	  Pointer to vector iteration context
 * \param result  Pointer to location to receive resulting style
 * \param prop    The property being parsed
 * \return CSS_OK on success,
 *	   CSS_NOMEM on memory exhaustion,
 *	   CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *		   If the input is invalid, then \a *ctx remains unchanged.
 */
static css_error parse_corner(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result, enum css_properties_e prop)
{
	int32_t orig_ctx = *ctx;
	const css_token *token;
	enum flag_value flag_value;
	css_fixed length[2] = { 0, 0 };
	uint32_t unit[2] = { UNIT_PX, UNIT_PX };
	css_error error;

	/* [ length | percentage ]{1,2} */
	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	flag_value = get_css_flag_value(c, token);
	if (flag_value != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		return css_stylesheet_style_flag_value(result, flag_value,
				prop);
	}

	error = parse_radius(c, vector, ctx, &length[0], &unit[0]);
	if (error != CSS_OK) {
		*ctx = orig_ctx;
		return error;
	}

	/* The vertical radius is the horizontal one if omitted */
	length[1] = length[0];
	unit[1] = unit[0];
	consumeWhitespace(vector, ctx);
	if (parserutils_vector_peek(vector, *ctx) != NULL) {
		int32_t v_ctx = *ctx;

		if (parse_radius(c, vector, ctx, &length[1], &unit[1]) !=
				CSS_OK) {
			*ctx = v_ctx;
			length[1] = length[0];
			unit[1] = unit[0];
		}
	}

	error = append_corner(result, prop, length[0], unit[0],
			length[1], unit[1]);
	if (error != CSS_OK)
		*ctx = orig_ctx;

	return error;
}

css_error css__parse_border_top_left_radius(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return parse_corner(c, vector, ctx, result,
			CSS_PROP_BORDER_TOP_LEFT_RADIUS);
}

css_error css__parse_border_top_right_radius(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return parse_corner(c, vector, ctx, result,
			CSS_PROP_BORDER_TOP_RIGHT_RADIUS);
}

css_error css__parse_border_bottom_right_radius(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return parse_corner(c, vector, ctx, result,
			CSS_PROP_BORDER_BOTTOM_RIGHT_RADIUS);
}

css_error css__parse_border_bottom_left_radius(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return parse_corner(c, vector, ctx, result,
			CSS_PROP_BORDER_BOTTOM_LEFT_RADIUS);
}

/**
 * Parse up to four radii
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx     Pointer to vector iteration context
 * \param length  Updated with the radii
 * \param unit    Updated with the units of the radii
 * \param count   Updated with the number of radii
 * \return CSS_OK on success, CSS_INVALID if there are none
 */
static css_error parse_radii(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_fixed length[4], uint32_t unit[4], int *count)
{
	const css_token *token;

	*count = 0;

	while (*count < 4) {
		if (parse_radius(c, vector, ctx, &length[*count],
				&unit[*count]) != CSS_OK)
			break;
		(*count)++;

		consumeWhitespace(vector, ctx);
		token = parserutils_vector_peek(vector, *ctx);
		if (token == NULL || tokenIsChar(token, '/'))
			break;
	}

	return (*count > 0) ? CSS_OK : CSS_INVALID;
}

/**
 * Parse the border-radius shorthand
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
css_error css__parse_border_radius(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	int32_t orig_ctx = *ctx;
	const css_token *token;
	enum flag_value flag_value;
	css_fixed hlength[4], vlength[4];
	uint32_t hunit[4], vunit[4];
	int hcount, vcount;
	/* Which of the values given a corner takes, for 1 to 4 values:
	 * top left, top right, bottom right, bottom left */
	static const int pick[5][4] = {
		{ 0, 0, 0, 0 },
		{ 0, 0, 0, 0 },
		{ 0, 1, 0, 1 },
		{ 0, 1, 2, 1 },
		{ 0, 1, 2, 3 }
	};
	css_error error;
	int i;

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	flag_value = get_css_flag_value(c, token);
	if (flag_value != FLAG_VALUE__NONE) {
		for (i = 0; i < 4; i++) {
			error = css_stylesheet_style_flag_value(result,
					flag_value, corner_prop[i]);
			if (error != CSS_OK)
				return error;
		}
		parserutils_vector_iterate(vector, ctx);
		return CSS_OK;
	}

	/* [ length | percentage ]{1,4} [ / [ length | percentage ]{1,4} ]? */
	error = parse_radii(c, vector, ctx, hlength, hunit, &hcount);
	if (error != CSS_OK) {
		*ctx = orig_ctx;
		return error;
	}

	token = parserutils_vector_peek(vector, *ctx);
	if (token != NULL && tokenIsChar(token, '/')) {
		parserutils_vector_iterate(vector, ctx);
		consumeWhitespace(vector, ctx);

		error = parse_radii(c, vector, ctx, vlength, vunit, &vcount);
		if (error != CSS_OK) {
			*ctx = orig_ctx;
			return error;
		}
	} else {
		/* the vertical radii are the horizontal ones */
		vcount = hcount;
		memcpy(vlength, hlength, sizeof(vlength));
		memcpy(vunit, hunit, sizeof(vunit));
	}

	for (i = 0; i < 4; i++) {
		int h = pick[hcount][i];
		int v = pick[vcount][i];

		error = append_corner(result, corner_prop[i],
				hlength[h], hunit[h], vlength[v], vunit[v]);
		if (error != CSS_OK) {
			*ctx = orig_ctx;
			return error;
		}
	}

	return CSS_OK;
}
