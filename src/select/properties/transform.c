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

css_error css__cascade_transform(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_TRANSFORM_INHERIT;
	css_fixed x = 0;
	css_fixed y = 0;
	uint32_t x_unit = UNIT_PX;
	uint32_t y_unit = UNIT_PX;

	if (hasFlagValue(opv) == false) {
		switch (getValue(opv)) {
		case TRANSFORM_NONE:
			value = CSS_TRANSFORM_NONE;
			break;
		case TRANSFORM_TRANSLATE:
			value = CSS_TRANSFORM_TRANSLATE;

			x = *((css_fixed *) style->bytecode);
			advance_bytecode(style, sizeof(x));
			x_unit = *((uint32_t *) style->bytecode);
			advance_bytecode(style, sizeof(x_unit));
			y = *((css_fixed *) style->bytecode);
			advance_bytecode(style, sizeof(y));
			y_unit = *((uint32_t *) style->bytecode);
			advance_bytecode(style, sizeof(y_unit));
			break;
		}
	}

	x_unit = css__to_css_unit(x_unit);
	y_unit = css__to_css_unit(y_unit);

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_transform(state->computed, value,
				x, x_unit, y, y_unit);
	}

	return CSS_OK;
}

css_error css__set_transform_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_transform(style, hint->status,
			hint->data.position.h.value,
			hint->data.position.h.unit,
			hint->data.position.v.value,
			hint->data.position.v.unit);
}

css_error css__initial_transform(css_select_state *state)
{
	return set_transform(state->computed, CSS_TRANSFORM_NONE,
			0, CSS_UNIT_PX, 0, CSS_UNIT_PX);
}

css_error css__copy_transform(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed x = 0, y = 0;
	css_unit x_unit = CSS_UNIT_PX, y_unit = CSS_UNIT_PX;
	uint8_t type = get_transform(from, &x, &x_unit, &y, &y_unit);

	if (from == to) {
		return CSS_OK;
	}

	return set_transform(to, type, x, x_unit, y, y_unit);
}

css_error css__compose_transform(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed x = 0, y = 0;
	css_unit x_unit = CSS_UNIT_PX, y_unit = CSS_UNIT_PX;
	uint8_t type = get_transform(child, &x, &x_unit, &y, &y_unit);

	return css__copy_transform(
			type == CSS_TRANSFORM_INHERIT ? parent : child,
			result);
}
