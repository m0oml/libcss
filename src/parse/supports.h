/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2026 Trevor Cockayne
 */

#ifndef css_parse_supports_h_
#define css_parse_supports_h_

#include <parserutils/utils/vector.h>

#include "parse/language.h"

/**
 * Parse and evaluate the condition of an @supports rule.
 *
 * A declaration is supported if its property is known and its value
 * parses.  selector() and other functions are not supported.
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens, holding the condition
 * \param ctx     Pointer to vector iteration context
 * \param result  Updated to whether the condition holds
 * \return CSS_OK on success, CSS_INVALID if the condition is not valid
 */
css_error css__parse_supports(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, bool *result);

#endif
