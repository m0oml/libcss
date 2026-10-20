/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2026 Trevor Cockayne
 */

#include <stdlib.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "utils/utils.h"

#include "select/properties/properties.h"
#include "select/properties/helpers.h"

/**
 * Copy an array of shadows, including its end marker
 */
static css_error copy_shadows(const css_computed_shadow *shadows,
		css_computed_shadow **copy)
{
	size_t n = 0;

	*copy = NULL;
	if (shadows == NULL)
		return CSS_OK;

	while ((shadows[n].flags & CSS_COMPUTED_SHADOW_END) == 0)
		n++;

	*copy = malloc((n + 1) * sizeof(**copy));
	if (*copy == NULL)
		return CSS_NOMEM;

	memcpy(*copy, shadows, (n + 1) * sizeof(**copy));
	return CSS_OK;
}

css_error css__cascade_box_shadow(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_BOX_SHADOW_INHERIT;
	css_computed_shadow *shadows = NULL;
	css_error error;

	if (hasFlagValue(opv) == false) {
		value = CSS_BOX_SHADOW_NONE;

		if (getValue(opv) == BOX_SHADOW_SET) {
			uint32_t count = *((uint32_t *) style->bytecode);
			uint32_t i;

			advance_bytecode(style, sizeof(count));

			/* Zeroed, as styles are compared with memcmp() */
			shadows = calloc(count + 1, sizeof(*shadows));
			if (shadows == NULL)
				return CSS_NOMEM;

			for (i = 0; i < count; i++) {
				css_computed_shadow *s = &shadows[i];
				css_code_t *code = style->bytecode;
				uint32_t flags = code[0];

				s->flags = 0;
				if (flags & BOX_SHADOW_INSET)
					s->flags |= CSS_COMPUTED_SHADOW_INSET;
				if (flags & BOX_SHADOW_CURRENT_COLOR)
					s->flags |=
						CSS_COMPUTED_SHADOW_CURRENT_COLOR;
				s->x = (css_fixed) code[1];
				s->x_unit = css__to_css_unit(code[2]);
				s->y = (css_fixed) code[3];
				s->y_unit = css__to_css_unit(code[4]);
				s->blur = (css_fixed) code[5];
				s->blur_unit = css__to_css_unit(code[6]);
				s->spread = (css_fixed) code[7];
				s->spread_unit = css__to_css_unit(code[8]);
				s->color = code[9];

				advance_bytecode(style, 10 * sizeof(css_code_t));
			}

			memset(&shadows[count], 0, sizeof(shadows[count]));
			shadows[count].flags = CSS_COMPUTED_SHADOW_END;
			value = CSS_BOX_SHADOW_SET;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		error = set_box_shadow(state->computed, value, shadows);
		if (error != CSS_OK)
			free(shadows);
		return error;
	}

	free(shadows);
	return CSS_OK;
}

css_error css__set_box_shadow_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_box_shadow(style, hint->status, NULL);
}

css_error css__initial_box_shadow(css_select_state *state)
{
	return set_box_shadow(state->computed, CSS_BOX_SHADOW_NONE, NULL);
}

css_error css__copy_box_shadow(
		const css_computed_style *from,
		css_computed_style *to)
{
	const css_computed_shadow *shadows = NULL;
	css_computed_shadow *copy = NULL;
	uint8_t type = get_box_shadow(from, &shadows);
	css_error error;

	if (from == to) {
		return CSS_OK;
	}

	error = copy_shadows(shadows, &copy);
	if (error != CSS_OK) {
		return error;
	}

	error = set_box_shadow(to, type, copy);
	if (error != CSS_OK) {
		free(copy);
	}

	return error;
}

css_error css__compose_box_shadow(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	const css_computed_shadow *shadows = NULL;
	uint8_t type = get_box_shadow(child, &shadows);

	return css__copy_box_shadow(
			type == CSS_BOX_SHADOW_INHERIT ? parent : child,
			result);
}
