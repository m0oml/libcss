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
#include "utils/utils.h"

/** The most arguments a transform function is looked at for */
#define TRANSFORM_MAX_ARGS 16

/** A translation along one axis, as a length and a unit */
struct translation {
	css_fixed length;
	uint32_t unit;
};

/**
 * Add a distance to a translation.
 *
 * Distances in different units cannot be added, so the first one which
 * is not zero is kept.
 *
 * \param t       The translation to add to
 * \param length  The distance
 * \param unit    The unit of the distance
 */
static void translation_add(struct translation *t,
		css_fixed length, uint32_t unit)
{
	if (length == 0) {
		return;
	}

	if (t->length == 0) {
		t->length = length;
		t->unit = unit;
	} else if (t->unit == unit) {
		t->length += length;
	}
}

/**
 * Parse an argument of a translation function, a length or percentage
 *
 * \param c        Parsing context
 * \param vector   Vector of tokens to process
 * \param arg_ctx  Position of the argument's token in the vector
 * \param t        The translation to add the argument to
 */
static void parse_translate_arg(css_language *c,
		const parserutils_vector *vector, int32_t arg_ctx,
		struct translation *t)
{
	const css_token *token = parserutils_vector_peek(vector, arg_ctx);
	css_fixed length = 0;
	uint32_t unit = UNIT_PX;

	if (token == NULL ||
			(token->type != CSS_TOKEN_DIMENSION &&
			 token->type != CSS_TOKEN_NUMBER &&
			 token->type != CSS_TOKEN_PERCENTAGE)) {
		return;
	}

	if (css__parse_unit_specifier(c, vector, &arg_ctx, UNIT_PX,
			&length, &unit) != CSS_OK) {
		return;
	}

	/* a number is only a length if it is zero */
	if ((unit & (UNIT_LENGTH | UNIT_PCT)) == 0 ||
			token->type == CSS_TOKEN_NUMBER) {
		return;
	}

	translation_add(t, length, unit);
}

/**
 * Parse an argument of a matrix function, a number which is a distance
 * in pixels
 *
 * \param vector   Vector of tokens to process
 * \param arg_ctx  Position of the argument's token in the vector
 * \param t        The translation to add the argument to
 */
static void parse_matrix_arg(const parserutils_vector *vector,
		int32_t arg_ctx, struct translation *t)
{
	const css_token *token = parserutils_vector_peek(vector, arg_ctx);
	size_t consumed = 0;
	css_fixed number;

	if (token == NULL || token->type != CSS_TOKEN_NUMBER) {
		return;
	}

	number = css__number_from_lwc_string(token->idata, false, &consumed);
	if (consumed != lwc_string_length(token->idata)) {
		return;
	}

	translation_add(t, number, UNIT_PX);
}

/**
 * Check if a function token is the named function
 */
static bool is_function(css_language *c, const css_token *token,
		int name)
{
	bool match;

	return lwc_string_caseless_isequal(token->idata, c->strings[name],
			&match) == lwc_error_ok && match;
}

/**
 * Parse transform
 *
 * Only the translation the functions make is kept.  Other functions,
 * such as rotations and scales, are accepted and have no effect.
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
css_error css__parse_transform(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	int32_t orig_ctx = *ctx;
	struct translation x = { 0, UNIT_PX };
	struct translation y = { 0, UNIT_PX };
	const css_token *token;
	enum flag_value flag_value;
	int functions = 0;
	css_error error;
	bool match;

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	flag_value = get_css_flag_value(c, token);
	if (flag_value != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		return css_stylesheet_style_flag_value(result, flag_value,
				CSS_PROP_TRANSFORM);
	}

	if (token->type == CSS_TOKEN_IDENT &&
			lwc_string_caseless_isequal(token->idata,
			c->strings[NONE], &match) == lwc_error_ok && match) {
		parserutils_vector_iterate(vector, ctx);
		return css__stylesheet_style_appendOPV(result,
				CSS_PROP_TRANSFORM, 0, TRANSFORM_NONE);
	}

	/* FUNCTION [ argument [ CHAR(,) argument ]* ] CHAR()) ... */
	while (true) {
		const css_token *function;
		int32_t arg_ctx[TRANSFORM_MAX_ARGS];
		int args = 0;
		int depth = 0;
		bool arg_start = true;
		bool closed = false;

		consumeWhitespace(vector, ctx);
		function = parserutils_vector_iterate(vector, ctx);
		if (function == NULL)
			break;

		if (function->type != CSS_TOKEN_FUNCTION) {
			*ctx = orig_ctx;
			return CSS_INVALID;
		}

		/* find where each argument starts, skipping any nested
		 * functions */
		while ((token = parserutils_vector_iterate(vector, ctx)) !=
				NULL) {
			if (token->type == CSS_TOKEN_S)
				continue;

			if (depth == 0 && tokenIsChar(token, ')')) {
				closed = true;
				break;
			}

			if (depth == 0 && tokenIsChar(token, ',')) {
				arg_start = true;
				continue;
			}

			if (depth == 0 && arg_start) {
				if (args < TRANSFORM_MAX_ARGS &&
						token->type != CSS_TOKEN_FUNCTION)
					arg_ctx[args] = *ctx - 1;
				else if (args < TRANSFORM_MAX_ARGS)
					arg_ctx[args] = -1;
				args++;
				arg_start = false;
			}

			if (token->type == CSS_TOKEN_FUNCTION ||
					tokenIsChar(token, '('))
				depth++;
			else if (tokenIsChar(token, ')'))
				depth--;
		}

		if (closed == false) {
			*ctx = orig_ctx;
			return CSS_INVALID;
		}

		if (args > TRANSFORM_MAX_ARGS)
			args = TRANSFORM_MAX_ARGS;

		if (is_function(c, function, TRANSLATE) ||
				is_function(c, function, TRANSLATE3D)) {
			if (args > 0 && arg_ctx[0] >= 0)
				parse_translate_arg(c, vector, arg_ctx[0], &x);
			if (args > 1 && arg_ctx[1] >= 0)
				parse_translate_arg(c, vector, arg_ctx[1], &y);
		} else if (is_function(c, function, TRANSLATEX)) {
			if (args > 0 && arg_ctx[0] >= 0)
				parse_translate_arg(c, vector, arg_ctx[0], &x);
		} else if (is_function(c, function, TRANSLATEY)) {
			if (args > 0 && arg_ctx[0] >= 0)
				parse_translate_arg(c, vector, arg_ctx[0], &y);
		} else if (is_function(c, function, MATRIX) && args == 6) {
			if (arg_ctx[4] >= 0)
				parse_matrix_arg(vector, arg_ctx[4], &x);
			if (arg_ctx[5] >= 0)
				parse_matrix_arg(vector, arg_ctx[5], &y);
		} else if (is_function(c, function, MATRIX3D) && args == 16) {
			if (arg_ctx[12] >= 0)
				parse_matrix_arg(vector, arg_ctx[12], &x);
			if (arg_ctx[13] >= 0)
				parse_matrix_arg(vector, arg_ctx[13], &y);
		}

		functions++;
	}

	if (functions == 0) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}

	error = css__stylesheet_style_appendOPV(result, CSS_PROP_TRANSFORM, 0,
			TRANSFORM_TRANSLATE);
	if (error != CSS_OK) {
		*ctx = orig_ctx;
		return error;
	}

	error = css__stylesheet_style_vappend(result, 4,
			x.length, x.unit, y.length, y.unit);
	if (error != CSS_OK)
		*ctx = orig_ctx;

	return error;
}
