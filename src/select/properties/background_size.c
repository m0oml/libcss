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

css_error css__cascade_background_size(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_BACKGROUND_SIZE_INHERIT;
	css_fixed width = 0;
	css_fixed height = 0;
	uint32_t wunit = UNIT_PX;
	uint32_t hunit = UNIT_PX;

	if (hasFlagValue(opv) == false) {
		uint16_t v = getValue(opv);

		if (v == BACKGROUND_SIZE_COVER) {
			value = CSS_BACKGROUND_SIZE_COVER;
		} else if (v == BACKGROUND_SIZE_CONTAIN) {
			value = CSS_BACKGROUND_SIZE_CONTAIN;
		} else {
			if (v & BACKGROUND_SIZE_WIDTH_SET) {
				width = *((css_fixed *) style->bytecode);
				advance_bytecode(style, sizeof(width));
				wunit = *((uint32_t *) style->bytecode);
				advance_bytecode(style, sizeof(wunit));
			}
			if (v & BACKGROUND_SIZE_HEIGHT_SET) {
				height = *((css_fixed *) style->bytecode);
				advance_bytecode(style, sizeof(height));
				hunit = *((uint32_t *) style->bytecode);
				advance_bytecode(style, sizeof(hunit));
			}

			if ((v & BACKGROUND_SIZE_WIDTH_SET) &&
					(v & BACKGROUND_SIZE_HEIGHT_SET))
				value = CSS_BACKGROUND_SIZE_SET;
			else if (v & BACKGROUND_SIZE_WIDTH_SET)
				value = CSS_BACKGROUND_SIZE_WIDTH;
			else if (v & BACKGROUND_SIZE_HEIGHT_SET)
				value = CSS_BACKGROUND_SIZE_HEIGHT;
			else
				value = CSS_BACKGROUND_SIZE_AUTO;
		}
	}

	wunit = css__to_css_unit(wunit);
	hunit = css__to_css_unit(hunit);

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_background_size(state->computed, value,
				width, wunit, height, hunit);
	}

	return CSS_OK;
}

css_error css__set_background_size_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_background_size(style, hint->status,
		hint->data.position.h.value, hint->data.position.h.unit,
		hint->data.position.v.value, hint->data.position.v.unit);
}

css_error css__initial_background_size(css_select_state *state)
{
	return set_background_size(state->computed,
			CSS_BACKGROUND_SIZE_AUTO,
			0, CSS_UNIT_PX, 0, CSS_UNIT_PX);
}

css_error css__copy_background_size(
		const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed width = 0, height = 0;
	css_unit wunit = CSS_UNIT_PX, hunit = CSS_UNIT_PX;
	uint8_t type = get_background_size(from, &width, &wunit,
			&height, &hunit);

	if (from == to) {
		return CSS_OK;
	}

	return set_background_size(to, type, width, wunit, height, hunit);
}

css_error css__compose_background_size(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed width = 0, height = 0;
	css_unit wunit = CSS_UNIT_PX, hunit = CSS_UNIT_PX;
	uint8_t type = get_background_size(child, &width, &wunit,
			&height, &hunit);

	return css__copy_background_size(
			type == CSS_BACKGROUND_SIZE_INHERIT ? parent : child,
			result);
}
