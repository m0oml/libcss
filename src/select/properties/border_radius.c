/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2026 Trevor Cockayne
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "utils/utils.h"

#include "select/properties/properties.h"
#include "select/properties/helpers.h"

/*
 * The four corners are handled in the same way, by these functions made
 * for each.
 */
#define BORDER_RADIUS_FUNCS(NAME)					\
css_error css__cascade_border_##NAME##_radius(uint32_t opv,		\
		css_style *style, css_select_state *state)		\
{									\
	uint16_t value = CSS_BORDER_RADIUS_INHERIT;			\
	css_fixed hlength = 0;						\
	css_fixed vlength = 0;						\
	uint32_t hunit = UNIT_PX;					\
	uint32_t vunit = UNIT_PX;					\
									\
	if (hasFlagValue(opv) == false) {				\
		value = CSS_BORDER_RADIUS_SET;				\
									\
		hlength = *((css_fixed *) style->bytecode);		\
		advance_bytecode(style, sizeof(hlength));		\
		hunit = *((uint32_t *) style->bytecode);		\
		advance_bytecode(style, sizeof(hunit));			\
		vlength = *((css_fixed *) style->bytecode);		\
		advance_bytecode(style, sizeof(vlength));		\
		vunit = *((uint32_t *) style->bytecode);		\
		advance_bytecode(style, sizeof(vunit));			\
	}								\
									\
	hunit = css__to_css_unit(hunit);				\
	vunit = css__to_css_unit(vunit);				\
									\
	if (css__outranks_existing(getOpcode(opv), isImportant(opv),	\
			state, getFlagValue(opv))) {			\
		return set_border_##NAME##_radius(state->computed,	\
				value, hlength, hunit, vlength, vunit);	\
	}								\
									\
	return CSS_OK;							\
}									\
									\
css_error css__set_border_##NAME##_radius_from_hint(			\
		const css_hint *hint, css_computed_style *style)	\
{									\
	return set_border_##NAME##_radius(style, hint->status,		\
		hint->data.position.h.value,				\
		hint->data.position.h.unit,				\
		hint->data.position.v.value,				\
		hint->data.position.v.unit);				\
}									\
									\
css_error css__initial_border_##NAME##_radius(css_select_state *state)	\
{									\
	return set_border_##NAME##_radius(state->computed,		\
			CSS_BORDER_RADIUS_SET,				\
			0, CSS_UNIT_PX, 0, CSS_UNIT_PX);		\
}									\
									\
css_error css__copy_border_##NAME##_radius(				\
		const css_computed_style *from,				\
		css_computed_style *to)					\
{									\
	css_fixed hlength = 0, vlength = 0;				\
	css_unit hunit = CSS_UNIT_PX, vunit = CSS_UNIT_PX;		\
	uint8_t type = get_border_##NAME##_radius(from, &hlength,	\
			&hunit, &vlength, &vunit);			\
									\
	if (from == to) {						\
		return CSS_OK;						\
	}								\
									\
	return set_border_##NAME##_radius(to, type, hlength, hunit,	\
			vlength, vunit);				\
}									\
									\
css_error css__compose_border_##NAME##_radius(				\
		const css_computed_style *parent,			\
		const css_computed_style *child,			\
		css_computed_style *result)				\
{									\
	css_fixed hlength = 0, vlength = 0;				\
	css_unit hunit = CSS_UNIT_PX, vunit = CSS_UNIT_PX;		\
	uint8_t type = get_border_##NAME##_radius(child, &hlength,	\
			&hunit, &vlength, &vunit);			\
									\
	return css__copy_border_##NAME##_radius(			\
			type == CSS_BORDER_RADIUS_INHERIT ?		\
					parent : child,			\
			result);					\
}

BORDER_RADIUS_FUNCS(top_left)
BORDER_RADIUS_FUNCS(top_right)
BORDER_RADIUS_FUNCS(bottom_right)
BORDER_RADIUS_FUNCS(bottom_left)
