/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2026 Trevor Cockayne
 */

#ifndef css_select_vars_h_
#define css_select_vars_h_

#include <libcss/computed.h>

#include "bytecode/bytecode.h"
#include "select/select.h"

struct css_stylesheet;
struct css_var_source;

/**
 * A custom property and its value
 */
typedef struct css_custom_property {
	lwc_string *name;
	lwc_string *value;
} css_custom_property;

/**
 * A property set by a declaration which uses var()
 */
typedef struct css_var_pending {
	opcode_t op;			/**< Property set */
	lwc_string *name;		/**< Property declared, maybe shorthand */
	lwc_string *value;		/**< Value before substitution */
	struct css_var_source *source;	/**< Where the declaration came from */
} css_var_pending;

/**
 * Custom properties of a computed style
 *
 * In a partial style (from selection) these are the element's own custom
 * property declarations, whose values may use var(), and the properties
 * waiting for var() substitution.  In a composed style they are all the
 * custom properties of the element, including those inherited, with any
 * var() substituted, and nothing is pending.
 */
typedef struct css_computed_vars {
	uint32_t refcount;
	uint32_t n_custom;
	css_custom_property *custom;	/**< Sorted by name */
	uint32_t n_pending;
	css_var_pending *pending;
} css_computed_vars;

/**
 * Release a reference to a computed style's custom properties
 */
void css__computed_vars_unref(css_computed_vars *vars);

/**
 * Compare the custom properties of two computed styles
 */
bool css__computed_vars_equal(const css_computed_vars *a,
		const css_computed_vars *b);

/**
 * Release a stylesheet's var() source
 */
void css__var_source_unref(struct css_var_source *source);

/**
 * Record a custom property declaration during selection
 *
 * \param opv    The declaration's OPV
 * \param style  Bytecode positioned after the OPV, advanced past it
 * \param state  Selection state
 * \return CSS_OK on success, appropriate error otherwise
 */
css_error css__select_vars_custom(css_code_t opv, css_style *style,
		css_select_state *state);

/**
 * Record a declaration which uses var() during selection
 *
 * \param opv    The declaration's OPV
 * \param style  Bytecode positioned after the OPV, advanced past it
 * \param state  Selection state
 * \return CSS_OK on success, appropriate error otherwise
 */
css_error css__select_vars_pending(css_code_t opv, css_style *style,
		css_select_state *state);

/**
 * Attach the custom properties found during selection to the results
 *
 * For the root element, which is not composed with a parent, the
 * var() substitution is done here.
 *
 * \param state  Selection state
 * \param root   Whether the element is the root element
 * \return CSS_OK on success, appropriate error otherwise
 */
css_error css__select_vars_finish(css_select_state *state, bool root);

/**
 * Destroy the selection state's custom property data
 */
void css__select_vars_destroy(css_select_state *state);

/**
 * Do var() substitution for a style being composed with its parent
 *
 * \param parent    Composed parent style
 * \param child     Partial child style
 * \param resolved  Updated to a copy of \a child with var() substituted,
 *                  or NULL if \a child has nothing pending
 * \param vars      Updated to the custom properties of the composed style
 * \return CSS_OK on success, appropriate error otherwise
 */
css_error css__computed_vars_compose(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style **resolved, css_computed_vars **vars);

#endif
