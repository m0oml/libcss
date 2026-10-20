/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2026 Trevor Cockayne
 */

#ifndef css_parse_custom_h_
#define css_parse_custom_h_

#include <parserutils/utils/vector.h>

#include "parse/language.h"
#include "parse/properties/properties.h"

/**
 * Determine whether a property name is a custom property (--name)
 *
 * \param name  Property name
 * \return true if it is a custom property name
 */
bool css__is_custom_property_name(lwc_string *name);

/**
 * Determine whether a declaration value uses var()
 *
 * \param vector  Vector of tokens to examine
 * \param ctx     Index of the first token of the value
 * \return true if the value contains a var() function
 */
bool css__value_uses_var(const parserutils_vector *vector, int32_t ctx);

/**
 * Parse a custom property declaration
 *
 * \param c          Parsing context
 * \param name       Name of the custom property
 * \param vector     Vector of tokens to process
 * \param ctx        Pointer to vector iteration context
 * \param result     Style to append bytecode to
 * \param important  Updated to whether the declaration is !important
 * \return CSS_OK on success, appropriate error otherwise
 *
 * Post condition: \a *ctx is past the end of the declaration
 */
css_error css__parse_custom_property(css_language *c, lwc_string *name,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result, bool *important);

/**
 * Parse a declaration whose value uses var()
 *
 * The value cannot be parsed until the custom properties it refers to
 * are known, so its text is kept for each property the declaration sets.
 *
 * \param c          Parsing context
 * \param handler    Parser for the property, which may be a shorthand
 * \param name       Name of the property
 * \param vector     Vector of tokens to process
 * \param ctx        Pointer to vector iteration context
 * \param result     Style to append bytecode to
 * \param important  Updated to whether the declaration is !important
 * \return CSS_OK on success, appropriate error otherwise
 *
 * Post condition: \a *ctx is past the end of the declaration
 */
css_error css__parse_var_declaration(css_language *c,
		css_prop_handler handler, lwc_string *name,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result, bool *important);

#endif
