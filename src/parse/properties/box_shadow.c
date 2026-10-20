/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2026 Trevor Cockayne
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/** Number of bytecode words for each shadow */
#define SHADOW_WORDS 10

/**
 * One parsed shadow
 */
struct shadow {
	uint32_t flags;
	css_fixed length[4];	/* x, y, blur, spread */
	uint32_t unit[4];
	uint32_t colour;
};

/**
 * Parse one shadow
 *
 * [ inset? && <length>{2,4} && <color>? ]
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx     Pointer to vector iteration context
 * \param shadow  Updated to the shadow
 * \return CSS_OK on success, CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is at the ',' or end after the shadow
 */
static css_error parse_shadow(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		struct shadow *shadow)
{
	const css_token *token;
	bool have_inset = false;
	bool have_colour = false;
	int lengths = -1;
	css_error error;
	bool match;

	memset(shadow, 0, sizeof(*shadow));
	shadow->flags = BOX_SHADOW_CURRENT_COLOR;
	shadow->unit[0] = shadow->unit[1] = UNIT_PX;
	shadow->unit[2] = shadow->unit[3] = UNIT_PX;

	consumeWhitespace(vector, ctx);

	/* Stop at the next shadow or at !important */
	while ((token = parserutils_vector_peek(vector, *ctx)) != NULL &&
			tokenIsChar(token, ',') == false &&
			tokenIsChar(token, '!') == false) {
		if (token->type == CSS_TOKEN_DIMENSION ||
				token->type == CSS_TOKEN_NUMBER) {
			/* The lengths must be together */
			if (lengths != -1)
				return CSS_INVALID;

			lengths = 0;
			while (lengths < 4 && token != NULL &&
					(token->type == CSS_TOKEN_DIMENSION ||
					 token->type == CSS_TOKEN_NUMBER)) {
				error = css__parse_unit_specifier(c, vector,
						ctx, UNIT_PX,
						&shadow->length[lengths],
						&shadow->unit[lengths]);
				if (error != CSS_OK)
					return error;

				/* Lengths only, numbers only if zero */
				if ((shadow->unit[lengths] & UNIT_LENGTH) ==
						0 ||
				    (token->type == CSS_TOKEN_NUMBER &&
				     shadow->length[lengths] != 0))
					return CSS_INVALID;

				/* The blur radius may not be negative */
				if (lengths == 2 && shadow->length[2] < 0)
					return CSS_INVALID;

				lengths++;
				consumeWhitespace(vector, ctx);
				token = parserutils_vector_peek(vector, *ctx);
			}

			if (lengths < 2)
				return CSS_INVALID;
		} else if (token->type == CSS_TOKEN_IDENT &&
				lwc_string_caseless_isequal(token->idata,
						c->strings[INSET], &match) ==
						lwc_error_ok && match) {
			if (have_inset)
				return CSS_INVALID;
			have_inset = true;
			shadow->flags |= BOX_SHADOW_INSET;
			parserutils_vector_iterate(vector, ctx);
			consumeWhitespace(vector, ctx);
		} else {
			uint16_t value = 0;

			if (have_colour)
				return CSS_INVALID;

			error = css__parse_colour_specifier(c, vector, ctx,
					&value, &shadow->colour);
			if (error != CSS_OK)
				return error;

			have_colour = true;
			if (value != COLOR_CURRENT_COLOR)
				shadow->flags &= ~BOX_SHADOW_CURRENT_COLOR;
			consumeWhitespace(vector, ctx);
		}
	}

	return lengths >= 2 ? CSS_OK : CSS_INVALID;
}

/**
 * Parse box-shadow
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
css_error css__parse_box_shadow(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	int32_t orig_ctx = *ctx;
	const css_token *token;
	enum flag_value flag_value;
	struct shadow *shadows = NULL;
	uint32_t count = 0;
	uint32_t i;
	css_error error;
	bool match;

	/* none | <shadow> [ ',' <shadow> ]* */
	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	flag_value = get_css_flag_value(c, token);
	if (flag_value != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		return css_stylesheet_style_flag_value(result, flag_value,
				CSS_PROP_BOX_SHADOW);
	}

	if (token->type == CSS_TOKEN_IDENT &&
			lwc_string_caseless_isequal(token->idata,
					c->strings[NONE], &match) ==
					lwc_error_ok && match) {
		parserutils_vector_iterate(vector, ctx);
		return css__stylesheet_style_appendOPV(result,
				CSS_PROP_BOX_SHADOW, 0, BOX_SHADOW_NONE);
	}

	do {
		struct shadow *temp;

		temp = realloc(shadows, (count + 1) * sizeof(*shadows));
		if (temp == NULL) {
			error = CSS_NOMEM;
			goto failed;
		}
		shadows = temp;

		error = parse_shadow(c, vector, ctx, &shadows[count]);
		if (error != CSS_OK)
			goto failed;
		count++;

		token = parserutils_vector_peek(vector, *ctx);
		if (token != NULL && tokenIsChar(token, ',')) {
			parserutils_vector_iterate(vector, ctx);
		} else {
			break;
		}
	} while (true);

	error = css__stylesheet_style_appendOPV(result, CSS_PROP_BOX_SHADOW,
			0, BOX_SHADOW_SET);
	if (error == CSS_OK)
		error = css__stylesheet_style_append(result, count);

	for (i = 0; i < count && error == CSS_OK; i++) {
		struct shadow *s = &shadows[i];

		error = css__stylesheet_style_vappend(result, SHADOW_WORDS,
				s->flags,
				s->length[0], s->unit[0],
				s->length[1], s->unit[1],
				s->length[2], s->unit[2],
				s->length[3], s->unit[3],
				s->colour);
	}

	if (error != CSS_OK)
		goto failed;

	free(shadows);
	return CSS_OK;

failed:
	free(shadows);
	*ctx = orig_ctx;
	return error;
}
