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

/** Setter of a property which is a track list */
typedef css_error (*grid_set_tracks)(css_computed_style *style,
		uint8_t type, css_computed_grid_track *tracks);

/** Getter of a property which is a track list */
typedef uint8_t (*grid_get_tracks)(const css_computed_style *style,
		const css_computed_grid_track **tracks);

/** Setter of a property which is a grid line */
typedef css_error (*grid_set_line)(css_computed_style *style,
		uint8_t type, int32_t integer, lwc_string *name);

/** Getter of a property which is a grid line */
typedef uint8_t (*grid_get_line)(const css_computed_style *style,
		int32_t *integer, lwc_string **name);

/**
 * Read a track list from bytecode
 *
 * \param style   The style, with its bytecode at the count of entries
 * \param tracks  Updated to the track list, to be freed by the caller
 * \return CSS_OK on success, CSS_NOMEM on memory exhaustion
 */
static css_error grid_read_tracks(css_style *style,
		css_computed_grid_track **tracks)
{
	uint32_t count = *((uint32_t *) style->bytecode);
	css_computed_grid_track *t;
	uint32_t i;

	advance_bytecode(style, sizeof(count));

	/* Zeroed, as styles are compared with memcmp() */
	t = calloc(count + 1, sizeof(*t));

	for (i = 0; i < count; i++) {
		css_code_t *code = style->bytecode;

		if (t != NULL) {
			t[i].kind = code[0] & 0xff;
			t[i].flags = (code[0] >> 8) & 0xff;
			t[i].min_type = (code[0] >> 16) & 0xff;
			t[i].max_type = (code[0] >> 24) & 0xff;
			t[i].min = (css_fixed) code[1];
			t[i].min_unit = css__to_css_unit(code[2]);
			t[i].max = (css_fixed) code[3];
			t[i].max_unit = css__to_css_unit(code[4]);

			if (code[5] != 0) {
				lwc_string *name = NULL;

				css__stylesheet_string_get(style->sheet,
						code[5], &name);
				if (name != NULL)
					t[i].name = lwc_string_ref(name);
			}
		}

		advance_bytecode(style, GRID_TRACK_WORDS * sizeof(css_code_t));
	}

	if (t == NULL)
		return CSS_NOMEM;

	t[count].kind = CSS_COMPUTED_GRID_TRACK_END;
	*tracks = t;

	return CSS_OK;
}

/**
 * Cascade a property which is a track list
 *
 * \param opv        The operator-value
 * \param style      The style, with its bytecode after the opv
 * \param state      Selection state
 * \param op_value   The value of the opv which has a list
 * \param no_list    The computed value without a list
 * \param list       The computed value with a list
 * \param set        Setter of the property
 * \return CSS_OK on success, appropriate error otherwise
 */
static css_error grid_cascade_tracks(uint32_t opv, css_style *style,
		css_select_state *state, uint32_t op_value,
		uint8_t no_list, uint8_t list, grid_set_tracks set)
{
	css_computed_grid_track *tracks = NULL;
	uint8_t value = CSS_GRID_TEMPLATE_INHERIT;
	css_error error;

	if (hasFlagValue(opv) == false) {
		value = no_list;

		if (getValue(opv) == op_value) {
			error = grid_read_tracks(style, &tracks);
			if (error != CSS_OK)
				return error;

			value = list;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		error = set(state->computed, value, tracks);
		if (error != CSS_OK)
			css__grid_tracks_free(tracks);
		return error;
	}

	css__grid_tracks_free(tracks);
	return CSS_OK;
}

/**
 * Copy a property which is a track list
 */
static css_error grid_copy_tracks(const css_computed_style *from,
		css_computed_style *to, grid_get_tracks get,
		grid_set_tracks set)
{
	const css_computed_grid_track *tracks = NULL;
	css_computed_grid_track *copy = NULL;
	uint8_t type = get(from, &tracks);
	css_error error;

	if (from == to)
		return CSS_OK;

	error = css__grid_tracks_copy(tracks, &copy);
	if (error != CSS_OK)
		return error;

	error = set(to, type, copy);
	if (error != CSS_OK)
		css__grid_tracks_free(copy);

	return error;
}

/**
 * Compose a property which is a track list
 */
static css_error grid_compose_tracks(const css_computed_style *parent,
		const css_computed_style *child, css_computed_style *result,
		grid_get_tracks get, grid_set_tracks set)
{
	const css_computed_grid_track *tracks = NULL;
	uint8_t type = get(child, &tracks);

	return grid_copy_tracks(type == CSS_GRID_TEMPLATE_INHERIT ?
			parent : child, result, get, set);
}

/**
 * Cascade a property which is a grid line
 */
static css_error grid_cascade_line(uint32_t opv, css_style *style,
		css_select_state *state, grid_set_line set)
{
	uint8_t value = CSS_GRID_LINE_INHERIT;
	lwc_string *name = NULL;
	int32_t integer = 0;

	if (hasFlagValue(opv) == false) {
		uint32_t v = getValue(opv);

		switch (v & ~GRID_LINE_NAMED) {
		case GRID_LINE_SET:
			value = CSS_GRID_LINE_SET;
			break;
		case GRID_LINE_SPAN:
			value = CSS_GRID_LINE_SPAN;
			break;
		default:
			value = CSS_GRID_LINE_AUTO;
			break;
		}

		if (v != GRID_LINE_AUTO) {
			integer = *((int32_t *) style->bytecode);
			advance_bytecode(style, sizeof(css_code_t));
		}

		if (v & GRID_LINE_NAMED) {
			css__stylesheet_string_get(style->sheet,
					*((css_code_t *) style->bytecode),
					&name);
			advance_bytecode(style, sizeof(css_code_t));
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set(state->computed, value, integer, name);
	}

	return CSS_OK;
}

/**
 * Copy a property which is a grid line
 */
static css_error grid_copy_line(const css_computed_style *from,
		css_computed_style *to, grid_get_line get, grid_set_line set)
{
	lwc_string *name = NULL;
	int32_t integer = 0;
	uint8_t type = get(from, &integer, &name);

	if (from == to)
		return CSS_OK;

	return set(to, type, integer, name);
}

/**
 * Compose a property which is a grid line
 */
static css_error grid_compose_line(const css_computed_style *parent,
		const css_computed_style *child, css_computed_style *result,
		grid_get_line get, grid_set_line set)
{
	lwc_string *name = NULL;
	int32_t integer = 0;
	uint8_t type = get(child, &integer, &name);

	return grid_copy_line(type == CSS_GRID_LINE_INHERIT ? parent : child,
			result, get, set);
}

/* The getters take a non-const pointer to the list in propget.h */
static uint8_t grid_get_template_columns(const css_computed_style *style,
		const css_computed_grid_track **tracks)
{
	return get_grid_template_columns(style, tracks);
}

static uint8_t grid_get_template_rows(const css_computed_style *style,
		const css_computed_grid_track **tracks)
{
	return get_grid_template_rows(style, tracks);
}

static uint8_t grid_get_auto_columns(const css_computed_style *style,
		const css_computed_grid_track **tracks)
{
	return get_grid_auto_columns(style, tracks);
}

static uint8_t grid_get_auto_rows(const css_computed_style *style,
		const css_computed_grid_track **tracks)
{
	return get_grid_auto_rows(style, tracks);
}

/* grid-template-columns */

css_error css__cascade_grid_template_columns(uint32_t opv, css_style *style,
		css_select_state *state)
{
	return grid_cascade_tracks(opv, style, state, GRID_TEMPLATE_SET,
			CSS_GRID_TEMPLATE_NONE, CSS_GRID_TEMPLATE_SET,
			set_grid_template_columns);
}

css_error css__set_grid_template_columns_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_grid_template_columns(style, hint->status, NULL);
}

css_error css__initial_grid_template_columns(css_select_state *state)
{
	return set_grid_template_columns(state->computed,
			CSS_GRID_TEMPLATE_NONE, NULL);
}

css_error css__copy_grid_template_columns(const css_computed_style *from,
		css_computed_style *to)
{
	return grid_copy_tracks(from, to, grid_get_template_columns,
			set_grid_template_columns);
}

css_error css__compose_grid_template_columns(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	return grid_compose_tracks(parent, child, result,
			grid_get_template_columns, set_grid_template_columns);
}

/* grid-template-rows */

css_error css__cascade_grid_template_rows(uint32_t opv, css_style *style,
		css_select_state *state)
{
	return grid_cascade_tracks(opv, style, state, GRID_TEMPLATE_SET,
			CSS_GRID_TEMPLATE_NONE, CSS_GRID_TEMPLATE_SET,
			set_grid_template_rows);
}

css_error css__set_grid_template_rows_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_grid_template_rows(style, hint->status, NULL);
}

css_error css__initial_grid_template_rows(css_select_state *state)
{
	return set_grid_template_rows(state->computed,
			CSS_GRID_TEMPLATE_NONE, NULL);
}

css_error css__copy_grid_template_rows(const css_computed_style *from,
		css_computed_style *to)
{
	return grid_copy_tracks(from, to, grid_get_template_rows,
			set_grid_template_rows);
}

css_error css__compose_grid_template_rows(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	return grid_compose_tracks(parent, child, result,
			grid_get_template_rows, set_grid_template_rows);
}

/* grid-auto-columns */

css_error css__cascade_grid_auto_columns(uint32_t opv, css_style *style,
		css_select_state *state)
{
	return grid_cascade_tracks(opv, style, state, GRID_AUTO_SET,
			CSS_GRID_AUTO_AUTO, CSS_GRID_AUTO_SET,
			set_grid_auto_columns);
}

css_error css__set_grid_auto_columns_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_grid_auto_columns(style, hint->status, NULL);
}

css_error css__initial_grid_auto_columns(css_select_state *state)
{
	return set_grid_auto_columns(state->computed,
			CSS_GRID_AUTO_AUTO, NULL);
}

css_error css__copy_grid_auto_columns(const css_computed_style *from,
		css_computed_style *to)
{
	return grid_copy_tracks(from, to, grid_get_auto_columns,
			set_grid_auto_columns);
}

css_error css__compose_grid_auto_columns(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	return grid_compose_tracks(parent, child, result,
			grid_get_auto_columns, set_grid_auto_columns);
}

/* grid-auto-rows */

css_error css__cascade_grid_auto_rows(uint32_t opv, css_style *style,
		css_select_state *state)
{
	return grid_cascade_tracks(opv, style, state, GRID_AUTO_SET,
			CSS_GRID_AUTO_AUTO, CSS_GRID_AUTO_SET,
			set_grid_auto_rows);
}

css_error css__set_grid_auto_rows_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_grid_auto_rows(style, hint->status, NULL);
}

css_error css__initial_grid_auto_rows(css_select_state *state)
{
	return set_grid_auto_rows(state->computed, CSS_GRID_AUTO_AUTO, NULL);
}

css_error css__copy_grid_auto_rows(const css_computed_style *from,
		css_computed_style *to)
{
	return grid_copy_tracks(from, to, grid_get_auto_rows,
			set_grid_auto_rows);
}

css_error css__compose_grid_auto_rows(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	return grid_compose_tracks(parent, child, result,
			grid_get_auto_rows, set_grid_auto_rows);
}

/* grid-template-areas */

css_error css__cascade_grid_template_areas(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint8_t value = CSS_GRID_TEMPLATE_AREAS_INHERIT;
	css_computed_grid_area *areas = NULL;
	css_error error;

	if (hasFlagValue(opv) == false) {
		value = CSS_GRID_TEMPLATE_AREAS_NONE;

		if (getValue(opv) == GRID_TEMPLATE_AREAS_SET) {
			uint32_t count = style->bytecode[0];
			uint32_t rows = style->bytecode[1];
			uint32_t columns = style->bytecode[2];
			uint32_t i;

			advance_bytecode(style, 3 * sizeof(css_code_t));

			/* Zeroed, as styles are compared with memcmp() */
			areas = calloc(count + 1, sizeof(*areas));

			for (i = 0; i < count; i++) {
				css_code_t *code = style->bytecode;
				lwc_string *name = NULL;

				css__stylesheet_string_get(style->sheet,
						code[0], &name);
				if (areas != NULL && name != NULL) {
					areas[i].name = lwc_string_ref(name);
					areas[i].row_start = code[1];
					areas[i].row_end = code[2];
					areas[i].column_start = code[3];
					areas[i].column_end = code[4];
				}

				advance_bytecode(style, GRID_AREA_WORDS *
						sizeof(css_code_t));
			}

			if (areas == NULL)
				return CSS_NOMEM;

			areas[count].row_end = rows;
			areas[count].column_end = columns;
			value = CSS_GRID_TEMPLATE_AREAS_SET;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		error = set_grid_template_areas(state->computed, value, areas);
		if (error != CSS_OK)
			css__grid_areas_free(areas);
		return error;
	}

	css__grid_areas_free(areas);
	return CSS_OK;
}

css_error css__set_grid_template_areas_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_grid_template_areas(style, hint->status, NULL);
}

css_error css__initial_grid_template_areas(css_select_state *state)
{
	return set_grid_template_areas(state->computed,
			CSS_GRID_TEMPLATE_AREAS_NONE, NULL);
}

css_error css__copy_grid_template_areas(const css_computed_style *from,
		css_computed_style *to)
{
	const css_computed_grid_area *areas = NULL;
	css_computed_grid_area *copy = NULL;
	uint8_t type = get_grid_template_areas(from, &areas);
	css_error error;

	if (from == to)
		return CSS_OK;

	error = css__grid_areas_copy(areas, &copy);
	if (error != CSS_OK)
		return error;

	error = set_grid_template_areas(to, type, copy);
	if (error != CSS_OK)
		css__grid_areas_free(copy);

	return error;
}

css_error css__compose_grid_template_areas(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	const css_computed_grid_area *areas = NULL;
	uint8_t type = get_grid_template_areas(child, &areas);

	return css__copy_grid_template_areas(
			type == CSS_GRID_TEMPLATE_AREAS_INHERIT ?
			parent : child, result);
}

/* grid-column-start, grid-column-end, grid-row-start and grid-row-end */

#define GRID_LINE_PROPERTY(name)					\
css_error css__cascade_##name(uint32_t opv, css_style *style,		\
		css_select_state *state)				\
{									\
	return grid_cascade_line(opv, style, state, set_##name);	\
}									\
									\
css_error css__set_##name##_from_hint(const css_hint *hint,		\
		css_computed_style *style)				\
{									\
	return set_##name(style, hint->status, 0, NULL);		\
}									\
									\
css_error css__initial_##name(css_select_state *state)			\
{									\
	return set_##name(state->computed, CSS_GRID_LINE_AUTO, 0, NULL); \
}									\
									\
css_error css__copy_##name(const css_computed_style *from,		\
		css_computed_style *to)					\
{									\
	return grid_copy_line(from, to, get_##name, set_##name);	\
}									\
									\
css_error css__compose_##name(const css_computed_style *parent,	\
		const css_computed_style *child,			\
		css_computed_style *result)				\
{									\
	return grid_compose_line(parent, child, result,			\
			get_##name, set_##name);			\
}

GRID_LINE_PROPERTY(grid_column_start)
GRID_LINE_PROPERTY(grid_column_end)
GRID_LINE_PROPERTY(grid_row_start)
GRID_LINE_PROPERTY(grid_row_end)

#undef GRID_LINE_PROPERTY

/* grid-auto-flow, justify-items and justify-self, whose computed values
 * are one more than their bytecode values */

#define GRID_KEYWORD_PROPERTY(name, initial)				\
css_error css__cascade_##name(uint32_t opv, css_style *style,		\
		css_select_state *state)				\
{									\
	uint8_t value = 0;						\
									\
	UNUSED(style);							\
									\
	if (hasFlagValue(opv) == false)					\
		value = getValue(opv) + 1;				\
									\
	if (css__outranks_existing(getOpcode(opv), isImportant(opv),	\
			state, getFlagValue(opv))) {			\
		return set_##name(state->computed, value);		\
	}								\
									\
	return CSS_OK;							\
}									\
									\
css_error css__set_##name##_from_hint(const css_hint *hint,		\
		css_computed_style *style)				\
{									\
	return set_##name(style, hint->status);				\
}									\
									\
css_error css__initial_##name(css_select_state *state)			\
{									\
	return set_##name(state->computed, initial);			\
}									\
									\
css_error css__copy_##name(const css_computed_style *from,		\
		css_computed_style *to)					\
{									\
	if (from == to)							\
		return CSS_OK;						\
									\
	return set_##name(to, get_##name(from));			\
}									\
									\
css_error css__compose_##name(const css_computed_style *parent,	\
		const css_computed_style *child,			\
		css_computed_style *result)				\
{									\
	return css__copy_##name(get_##name(child) == 0 ?		\
			parent : child, result);			\
}

GRID_KEYWORD_PROPERTY(grid_auto_flow, CSS_GRID_AUTO_FLOW_ROW)
GRID_KEYWORD_PROPERTY(justify_items, CSS_JUSTIFY_ITEMS_STRETCH)
GRID_KEYWORD_PROPERTY(justify_self, CSS_JUSTIFY_SELF_AUTO)

#undef GRID_KEYWORD_PROPERTY

/* row-gap */

css_error css__cascade_row_gap(uint32_t opv, css_style *style,
		css_select_state *state)
{
	return css__cascade_length_normal(opv, style, state, set_row_gap);
}

css_error css__set_row_gap_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_row_gap(style, hint->status,
			hint->data.length.value, hint->data.length.unit);
}

css_error css__initial_row_gap(css_select_state *state)
{
	return set_row_gap(state->computed, CSS_ROW_GAP_NORMAL,
			0, CSS_UNIT_PX);
}

css_error css__copy_row_gap(const css_computed_style *from,
		css_computed_style *to)
{
	css_fixed length = 0;
	css_unit unit = CSS_UNIT_PX;
	uint8_t type = get_row_gap(from, &length, &unit);

	if (from == to)
		return CSS_OK;

	return set_row_gap(to, type, length, unit);
}

css_error css__compose_row_gap(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	css_fixed length = 0;
	css_unit unit = CSS_UNIT_PX;
	uint8_t type = get_row_gap(child, &length, &unit);

	return css__copy_row_gap(type == CSS_ROW_GAP_INHERIT ?
			parent : child, result);
}
