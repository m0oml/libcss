/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2026 Trevor Cockayne
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libcss/computed.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"
#include "utils/utils.h"

/** The most entries a track list may have, after repeat() is expanded */
#define GRID_MAX_TRACK_ENTRIES 1000

/** The most rows or columns grid-template-areas may have */
#define GRID_MAX_AREA_CELLS 1000

/** An entry in a track list being parsed */
struct grid_track {
	uint8_t kind;
	uint8_t flags;
	uint8_t min_type;
	uint8_t max_type;
	css_fixed min;
	uint32_t min_unit;
	css_fixed max;
	uint32_t max_unit;
	uint32_t name;		/**< String table entry, or 0 */
};

/** A track list being parsed */
struct grid_track_list {
	struct grid_track *entries;
	uint32_t count;
	uint32_t alloc;
	uint32_t sizes;		/**< Number of entries which are tracks */
	bool auto_repeat;	/**< An auto-fill or auto-fit repeat was seen */
};

/** A grid line, as grid-row-start and the like give */
struct grid_line {
	uint32_t value;		/**< op_grid_line value */
	int32_t integer;
	uint32_t name;		/**< String table entry, or 0 */
};

/**
 * Check if a token is the identifier given
 */
static bool grid_is_ident(css_language *c, const css_token *token,
		int string)
{
	bool match;

	return token != NULL && token->type == CSS_TOKEN_IDENT &&
			lwc_string_caseless_isequal(token->idata,
			c->strings[string], &match) == lwc_error_ok && match;
}

/**
 * Check if a token is a call of the function given
 */
static bool grid_is_function(css_language *c, const css_token *token,
		int string)
{
	bool match;

	return token != NULL && token->type == CSS_TOKEN_FUNCTION &&
			lwc_string_caseless_isequal(token->idata,
			c->strings[string], &match) == lwc_error_ok && match;
}

/**
 * Check if a token is an identifier which may name a grid line or area
 */
static bool grid_is_custom_ident(css_language *c, const css_token *token)
{
	return token != NULL && token->type == CSS_TOKEN_IDENT &&
			get_css_flag_value(c, token) == FLAG_VALUE__NONE &&
			grid_is_ident(c, token, SPAN) == false &&
			grid_is_ident(c, token, AUTO) == false &&
			grid_is_ident(c, token, DEFAULT) == false;
}

/**
 * Check if a token ends a value, as the end, an important flag or a slash
 */
static bool grid_is_end(const css_token *token)
{
	return token == NULL || tokenIsChar(token, '!') ||
			tokenIsChar(token, '/');
}

/**
 * Parse an integer
 *
 * \param token    The token
 * \param integer  Updated to the integer
 * \return true if the token is an integer
 */
static bool grid_parse_integer(const css_token *token, int32_t *integer)
{
	size_t consumed = 0;
	css_fixed number;

	if (token == NULL || token->type != CSS_TOKEN_NUMBER)
		return false;

	number = css__number_from_lwc_string(token->idata, true, &consumed);
	if (consumed != lwc_string_length(token->idata))
		return false;

	*integer = FIXTOINT(number);
	return true;
}

/**
 * Add an identifier to the string table
 *
 * \param c       Parsing context
 * \param token   The identifier token
 * \param string  Updated to the string table entry
 * \return CSS_OK on success, appropriate error otherwise
 */
static css_error grid_add_name(css_language *c, const css_token *token,
		uint32_t *string)
{
	return css__stylesheet_string_add(c->sheet,
			lwc_string_ref(token->idata), string);
}

/**
 * Parse a track breadth: a length, a percentage, a flexible length, auto,
 * min-content or max-content
 *
 * \param c           Parsing context
 * \param vector      Vector of tokens to process
 * \param ctx         Pointer to vector iteration context
 * \param allow_flex  Whether a flexible length is allowed
 * \param type        Updated to the css_computed_grid_sizing
 * \param value       Updated to the length or flex factor
 * \param unit        Updated to the unit of a length
 * \return true if a breadth was parsed, with \a ctx moved past it
 */
static bool grid_parse_breadth(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		bool allow_flex, uint8_t *type, css_fixed *value,
		uint32_t *unit)
{
	const css_token *token = parserutils_vector_peek(vector, *ctx);
	int32_t unit_ctx = *ctx;

	*value = 0;
	*unit = UNIT_PX;

	if (token == NULL)
		return false;

	if (token->type == CSS_TOKEN_IDENT) {
		if (grid_is_ident(c, token, AUTO))
			*type = CSS_COMPUTED_GRID_SIZING_AUTO;
		else if (grid_is_ident(c, token, MIN_CONTENT))
			*type = CSS_COMPUTED_GRID_SIZING_MIN_CONTENT;
		else if (grid_is_ident(c, token, MAX_CONTENT))
			*type = CSS_COMPUTED_GRID_SIZING_MAX_CONTENT;
		else
			return false;

		parserutils_vector_iterate(vector, ctx);
		return true;
	}

	if (token->type == CSS_TOKEN_DIMENSION) {
		const char *data = lwc_string_data(token->idata);
		size_t len = lwc_string_length(token->idata);
		size_t consumed = 0;
		css_fixed number;

		number = css__number_from_lwc_string(token->idata, false,
				&consumed);
		if (len - consumed == 2 &&
				strncasecmp(data + consumed, "fr", 2) == 0) {
			if (allow_flex == false || number < 0)
				return false;

			*type = CSS_COMPUTED_GRID_SIZING_FLEX;
			*value = number;
			parserutils_vector_iterate(vector, ctx);
			return true;
		}
	}

	if (token->type != CSS_TOKEN_DIMENSION &&
			token->type != CSS_TOKEN_NUMBER &&
			token->type != CSS_TOKEN_PERCENTAGE)
		return false;

	if (css__parse_unit_specifier(c, vector, &unit_ctx, UNIT_PX,
			value, unit) != CSS_OK)
		return false;

	/* A number is only a length if it is zero */
	if ((*unit & (UNIT_LENGTH | UNIT_PCT)) == 0 || *value < 0 ||
			(token->type == CSS_TOKEN_NUMBER && *value != 0))
		return false;

	*type = CSS_COMPUTED_GRID_SIZING_LENGTH;
	*ctx = unit_ctx;
	return true;
}

/**
 * Parse a length or percentage which is not negative
 */
static bool grid_parse_length(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_fixed *value, uint32_t *unit)
{
	int32_t length_ctx = *ctx;
	uint8_t type;

	if (grid_parse_breadth(c, vector, &length_ctx, false,
			&type, value, unit) == false ||
			type != CSS_COMPUTED_GRID_SIZING_LENGTH)
		return false;

	*ctx = length_ctx;
	return true;
}

/**
 * Parse a closing bracket of a function, after any whitespace
 */
static bool grid_parse_close(const parserutils_vector *vector, int32_t *ctx)
{
	consumeWhitespace(vector, ctx);
	if (tokenIsChar(parserutils_vector_peek(vector, *ctx), ')') == false)
		return false;

	parserutils_vector_iterate(vector, ctx);
	return true;
}

/**
 * Parse a track size: a breadth, minmax() or fit-content()
 *
 * \param c        Parsing context
 * \param vector   Vector of tokens to process
 * \param ctx      Pointer to vector iteration context
 * \param track    Updated to the track
 * \return true if a track size was parsed, with \a ctx moved past it
 */
static bool grid_parse_track_size(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		struct grid_track *track)
{
	const css_token *token = parserutils_vector_peek(vector, *ctx);
	int32_t size_ctx = *ctx;

	memset(track, 0, sizeof(*track));
	track->kind = CSS_COMPUTED_GRID_TRACK_SIZE;
	track->min_unit = UNIT_PX;
	track->max_unit = UNIT_PX;

	if (grid_is_function(c, token, MINMAX)) {
		parserutils_vector_iterate(vector, &size_ctx);
		consumeWhitespace(vector, &size_ctx);
		if (grid_parse_breadth(c, vector, &size_ctx, false,
				&track->min_type, &track->min,
				&track->min_unit) == false)
			return false;

		consumeWhitespace(vector, &size_ctx);
		if (tokenIsChar(parserutils_vector_iterate(vector, &size_ctx),
				',') == false)
			return false;

		consumeWhitespace(vector, &size_ctx);
		if (grid_parse_breadth(c, vector, &size_ctx, true,
				&track->max_type, &track->max,
				&track->max_unit) == false)
			return false;

		if (grid_parse_close(vector, &size_ctx) == false)
			return false;

		*ctx = size_ctx;
		return true;
	}

	if (grid_is_function(c, token, FIT_CONTENT)) {
		parserutils_vector_iterate(vector, &size_ctx);
		consumeWhitespace(vector, &size_ctx);
		if (grid_parse_length(c, vector, &size_ctx,
				&track->max, &track->max_unit) == false)
			return false;

		if (grid_parse_close(vector, &size_ctx) == false)
			return false;

		track->min_type = CSS_COMPUTED_GRID_SIZING_AUTO;
		track->max_type = CSS_COMPUTED_GRID_SIZING_FIT_CONTENT;
		*ctx = size_ctx;
		return true;
	}

	if (grid_parse_breadth(c, vector, &size_ctx, true, &track->max_type,
			&track->max, &track->max_unit) == false)
		return false;

	if (track->max_type == CSS_COMPUTED_GRID_SIZING_FLEX) {
		/* A flexible track has a minimum of auto */
		track->min_type = CSS_COMPUTED_GRID_SIZING_AUTO;
	} else {
		track->min_type = track->max_type;
		track->min = track->max;
		track->min_unit = track->max_unit;
	}

	*ctx = size_ctx;
	return true;
}

/**
 * Add an entry to a track list
 *
 * \return CSS_OK on success, CSS_NOMEM or CSS_INVALID if the list is full
 */
static css_error grid_list_add(struct grid_track_list *list,
		const struct grid_track *track)
{
	if (list->count >= GRID_MAX_TRACK_ENTRIES)
		return CSS_INVALID;

	if (list->count == list->alloc) {
		uint32_t alloc = list->alloc == 0 ? 8 : list->alloc * 2;
		struct grid_track *entries;

		entries = realloc(list->entries, alloc * sizeof(*entries));
		if (entries == NULL)
			return CSS_NOMEM;

		list->entries = entries;
		list->alloc = alloc;
	}

	list->entries[list->count++] = *track;
	if (track->kind == CSS_COMPUTED_GRID_TRACK_SIZE)
		list->sizes++;

	return CSS_OK;
}

/**
 * Parse line names in brackets, adding them to a track list
 *
 * \return CSS_OK on success, CSS_INVALID if the names are not valid
 */
static css_error grid_parse_line_names(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		struct grid_track_list *list)
{
	const css_token *token;
	css_error error;

	/* '[' has been peeked */
	parserutils_vector_iterate(vector, ctx);

	while (true) {
		struct grid_track name;

		consumeWhitespace(vector, ctx);
		token = parserutils_vector_iterate(vector, ctx);
		if (tokenIsChar(token, ']'))
			return CSS_OK;

		if (grid_is_custom_ident(c, token) == false)
			return CSS_INVALID;

		memset(&name, 0, sizeof(name));
		name.kind = CSS_COMPUTED_GRID_TRACK_NAME;
		name.min_unit = UNIT_PX;
		name.max_unit = UNIT_PX;

		error = grid_add_name(c, token, &name.name);
		if (error != CSS_OK)
			return error;

		error = grid_list_add(list, &name);
		if (error != CSS_OK)
			return error;
	}
}

/**
 * Parse repeat()
 *
 * Repeats with a count are expanded.  The entries of an auto-fill or
 * auto-fit repeat are added once, flagged.
 */
static css_error grid_parse_repeat(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		struct grid_track_list *list)
{
	struct grid_track_list inner = { NULL, 0, 0, 0, false };
	const css_token *token;
	int32_t count = 0;
	uint8_t flags = 0;
	css_error error = CSS_INVALID;
	uint32_t i;

	/* repeat( has been peeked */
	parserutils_vector_iterate(vector, ctx);
	consumeWhitespace(vector, ctx);
	token = parserutils_vector_iterate(vector, ctx);

	if (grid_is_ident(c, token, AUTO_FILL)) {
		flags = CSS_COMPUTED_GRID_TRACK_AUTO_FILL;
	} else if (grid_is_ident(c, token, AUTO_FIT)) {
		flags = CSS_COMPUTED_GRID_TRACK_AUTO_FIT;
	} else if (grid_parse_integer(token, &count) == false || count < 1) {
		return CSS_INVALID;
	}

	if (flags != 0 && list->auto_repeat)
		return CSS_INVALID;

	consumeWhitespace(vector, ctx);
	if (tokenIsChar(parserutils_vector_iterate(vector, ctx), ',') == false)
		return CSS_INVALID;

	while (true) {
		struct grid_track track;

		consumeWhitespace(vector, ctx);
		token = parserutils_vector_peek(vector, *ctx);

		if (tokenIsChar(token, ')')) {
			parserutils_vector_iterate(vector, ctx);
			break;
		} else if (tokenIsChar(token, '[')) {
			error = grid_parse_line_names(c, vector, ctx, &inner);
		} else if (grid_parse_track_size(c, vector, ctx, &track)) {
			/* An automatic repeat needs tracks of fixed size */
			if (flags != 0 &&
				(track.min_type != CSS_COMPUTED_GRID_SIZING_LENGTH &&
				 track.max_type != CSS_COMPUTED_GRID_SIZING_LENGTH))
				error = CSS_INVALID;
			else
				error = grid_list_add(&inner, &track);
		} else {
			error = CSS_INVALID;
		}

		if (error != CSS_OK)
			goto cleanup;
	}

	error = CSS_INVALID;
	if (inner.sizes == 0)
		goto cleanup;

	if (flags != 0) {
		count = 1;
		list->auto_repeat = true;
	}

	/* Stop a huge count before it fills the list */
	if ((uint32_t) count > GRID_MAX_TRACK_ENTRIES / inner.count)
		goto cleanup;

	error = CSS_OK;
	while (count-- > 0 && error == CSS_OK) {
		for (i = 0; i < inner.count && error == CSS_OK; i++) {
			inner.entries[i].flags = flags;
			error = grid_list_add(list, &inner.entries[i]);
		}
	}

cleanup:
	free(inner.entries);
	return error;
}

/**
 * Parse a track list, made of track sizes, line names and repeat()
 *
 * Parsing stops at the first token which is not part of the list.
 *
 * \param c            Parsing context
 * \param vector       Vector of tokens to process
 * \param ctx          Pointer to vector iteration context
 * \param allow_names  Whether line names and repeat() are allowed
 * \param list         The list to add to
 * \return CSS_OK on success, appropriate error otherwise
 */
static css_error grid_parse_track_list(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		bool allow_names, struct grid_track_list *list)
{
	const css_token *token;
	css_error error;

	while (true) {
		struct grid_track track;

		consumeWhitespace(vector, ctx);
		token = parserutils_vector_peek(vector, *ctx);

		if (allow_names && tokenIsChar(token, '[')) {
			error = grid_parse_line_names(c, vector, ctx, list);
		} else if (allow_names && grid_is_function(c, token, REPEAT)) {
			error = grid_parse_repeat(c, vector, ctx, list);
		} else if (grid_parse_track_size(c, vector, ctx, &track)) {
			error = grid_list_add(list, &track);
		} else {
			break;
		}

		if (error != CSS_OK)
			return error;
	}

	return list->sizes > 0 ? CSS_OK : CSS_INVALID;
}

/**
 * Append a track list to a style
 */
static css_error grid_append_track_list(css_style *result,
		enum css_properties_e prop, uint32_t value,
		const struct grid_track_list *list)
{
	css_error error;
	uint32_t i;

	error = css__stylesheet_style_appendOPV(result, prop, 0, value);
	if (error != CSS_OK)
		return error;

	error = css__stylesheet_style_append(result, list->count);
	if (error != CSS_OK)
		return error;

	for (i = 0; i < list->count; i++) {
		const struct grid_track *t = &list->entries[i];

		error = css__stylesheet_style_vappend(result,
				GRID_TRACK_WORDS,
				t->kind | (t->flags << 8) |
				(t->min_type << 16) | (t->max_type << 24),
				t->min, t->min_unit, t->max, t->max_unit,
				t->name);
		if (error != CSS_OK)
			return error;
	}

	return CSS_OK;
}

/**
 * Parse grid-template-columns or grid-template-rows
 */
static css_error grid_parse_template(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result, enum css_properties_e prop)
{
	struct grid_track_list list = { NULL, 0, 0, 0, false };
	int32_t orig_ctx = *ctx;
	const css_token *token;
	enum flag_value flag_value;
	css_error error;

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	flag_value = get_css_flag_value(c, token);
	if (flag_value != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		return css_stylesheet_style_flag_value(result, flag_value,
				prop);
	}

	if (grid_is_ident(c, token, NONE)) {
		parserutils_vector_iterate(vector, ctx);
		return css__stylesheet_style_appendOPV(result, prop, 0,
				GRID_TEMPLATE_NONE);
	}

	error = grid_parse_track_list(c, vector, ctx, true, &list);
	if (error == CSS_OK)
		error = grid_append_track_list(result, prop,
				GRID_TEMPLATE_SET, &list);

	free(list.entries);
	if (error != CSS_OK)
		*ctx = orig_ctx;

	return error;
}

/**
 * Parse grid-auto-columns or grid-auto-rows
 */
static css_error grid_parse_auto(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result, enum css_properties_e prop)
{
	struct grid_track_list list = { NULL, 0, 0, 0, false };
	int32_t orig_ctx = *ctx;
	const css_token *token;
	enum flag_value flag_value;
	css_error error;

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	flag_value = get_css_flag_value(c, token);
	if (flag_value != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		return css_stylesheet_style_flag_value(result, flag_value,
				prop);
	}

	error = grid_parse_track_list(c, vector, ctx, false, &list);
	if (error == CSS_OK) {
		if (list.count == 1 && list.entries[0].min_type ==
				CSS_COMPUTED_GRID_SIZING_AUTO &&
				list.entries[0].max_type ==
				CSS_COMPUTED_GRID_SIZING_AUTO)
			error = css__stylesheet_style_appendOPV(result, prop,
					0, GRID_AUTO_AUTO);
		else
			error = grid_append_track_list(result, prop,
					GRID_AUTO_SET, &list);
	}

	free(list.entries);
	if (error != CSS_OK)
		*ctx = orig_ctx;

	return error;
}

css_error css__parse_grid_template_columns(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return grid_parse_template(c, vector, ctx, result,
			CSS_PROP_GRID_TEMPLATE_COLUMNS);
}

css_error css__parse_grid_template_rows(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return grid_parse_template(c, vector, ctx, result,
			CSS_PROP_GRID_TEMPLATE_ROWS);
}

css_error css__parse_grid_auto_columns(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return grid_parse_auto(c, vector, ctx, result,
			CSS_PROP_GRID_AUTO_COLUMNS);
}

css_error css__parse_grid_auto_rows(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return grid_parse_auto(c, vector, ctx, result,
			CSS_PROP_GRID_AUTO_ROWS);
}

/** grid-template-areas being parsed */
struct grid_areas {
	lwc_string **cells;	/**< Names of cells, row by row, or NULL */
	uint32_t rows;
	uint32_t columns;
	uint32_t alloc;
};

/**
 * Add a row of grid-template-areas from a string
 *
 * \return CSS_OK on success, CSS_INVALID if the row does not fit
 */
static css_error grid_areas_add_row(struct grid_areas *areas,
		lwc_string *string)
{
	const char *data = lwc_string_data(string);
	size_t len = lwc_string_length(string);
	uint32_t base = areas->rows * areas->columns;
	uint32_t columns = 0;
	css_error error = CSS_OK;
	size_t i = 0;
	uint32_t j;

	while (true) {
		lwc_string *name = NULL;
		size_t start;

		while (i < len && (data[i] == ' ' || data[i] == '\t' ||
				data[i] == '\n' || data[i] == '\r' ||
				data[i] == '\f'))
			i++;

		if (i == len)
			break;

		if ((areas->rows > 0 && columns >= areas->columns) ||
				base + columns >= GRID_MAX_AREA_CELLS) {
			error = CSS_INVALID;
			break;
		}

		if (base + columns >= areas->alloc) {
			uint32_t alloc = areas->alloc == 0 ? 16 :
					areas->alloc * 2;
			lwc_string **cells;

			cells = realloc(areas->cells, alloc * sizeof(*cells));
			if (cells == NULL) {
				error = CSS_NOMEM;
				break;
			}

			areas->cells = cells;
			areas->alloc = alloc;
		}

		start = i;
		if (data[i] == '.') {
			/* A run of dots is an unnamed cell */
			while (i < len && data[i] == '.')
				i++;
		} else {
			while (i < len && data[i] != ' ' && data[i] != '\t' &&
					data[i] != '\n' && data[i] != '\r' &&
					data[i] != '\f' && data[i] != '.')
				i++;

			if (lwc_intern_string(data + start, i - start,
					&name) != lwc_error_ok) {
				error = CSS_NOMEM;
				break;
			}
		}

		areas->cells[base + columns] = name;
		columns++;
	}

	if (error == CSS_OK && (columns == 0 ||
			(areas->rows > 0 && columns != areas->columns)))
		error = CSS_INVALID;

	if (error != CSS_OK) {
		for (j = 0; j < columns; j++) {
			lwc_string_unref(areas->cells[base + j]);
		}
		return error;
	}

	areas->columns = columns;
	areas->rows++;

	return CSS_OK;
}

/**
 * Free the names of grid-template-areas being parsed
 */
static void grid_areas_free(struct grid_areas *areas)
{
	uint32_t i;

	for (i = 0; i < areas->rows * areas->columns; i++) {
		if (areas->cells[i] != NULL)
			lwc_string_unref(areas->cells[i]);
	}

	free(areas->cells);
}

/**
 * Append grid-template-areas to a style
 *
 * Each name must cover a rectangle of cells.
 *
 * \return CSS_OK on success, CSS_INVALID if an area is not a rectangle
 */
static css_error grid_append_areas(css_language *c, css_style *result,
		const struct grid_areas *areas)
{
	uint32_t cells = areas->rows * areas->columns;
	uint32_t count = 0;
	css_error error;
	uint32_t i, j;
	uint32_t used;

	/* Count the names, by their first cell */
	for (i = 0; i < cells; i++) {
		bool first = areas->cells[i] != NULL;

		for (j = 0; j < i && first; j++) {
			if (areas->cells[j] == areas->cells[i])
				first = false;
		}

		if (first)
			count++;
	}

	used = result->used;
	error = css__stylesheet_style_appendOPV(result,
			CSS_PROP_GRID_TEMPLATE_AREAS, 0,
			GRID_TEMPLATE_AREAS_SET);
	if (error == CSS_OK)
		error = css__stylesheet_style_vappend(result, 3, count,
				areas->rows, areas->columns);

	for (i = 0; i < cells && error == CSS_OK; i++) {
		lwc_string *name = areas->cells[i];
		uint32_t row = i / areas->columns;
		uint32_t column = i % areas->columns;
		uint32_t rows = 0, columns = 0;
		uint32_t found = 0;
		uint32_t string;
		bool first = name != NULL;

		for (j = 0; j < i && first; j++) {
			if (areas->cells[j] == name)
				first = false;
		}

		if (first == false)
			continue;

		/* The first cell is the top left; find the extent */
		while (column + columns < areas->columns &&
				areas->cells[i + columns] == name)
			columns++;
		while (row + rows < areas->rows &&
				areas->cells[i + rows * areas->columns] == name)
			rows++;

		for (j = i; j < cells; j++) {
			if (areas->cells[j] == name)
				found++;
		}

		if (found != rows * columns) {
			error = CSS_INVALID;
			break;
		}

		for (j = 0; j < rows * columns; j++) {
			if (areas->cells[i + (j / columns) * areas->columns +
					j % columns] != name)
				error = CSS_INVALID;
		}

		if (error != CSS_OK)
			break;

		error = css__stylesheet_string_add(c->sheet,
				lwc_string_ref(name), &string);
		if (error == CSS_OK)
			error = css__stylesheet_style_vappend(result,
					GRID_AREA_WORDS, string,
					row + 1, row + rows + 1,
					column + 1, column + columns + 1);
	}

	if (error != CSS_OK)
		result->used = used;

	return error;
}

css_error css__parse_grid_template_areas(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	struct grid_areas areas = { NULL, 0, 0, 0 };
	int32_t orig_ctx = *ctx;
	const css_token *token;
	enum flag_value flag_value;
	css_error error = CSS_OK;

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	flag_value = get_css_flag_value(c, token);
	if (flag_value != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		return css_stylesheet_style_flag_value(result, flag_value,
				CSS_PROP_GRID_TEMPLATE_AREAS);
	}

	if (grid_is_ident(c, token, NONE)) {
		parserutils_vector_iterate(vector, ctx);
		return css__stylesheet_style_appendOPV(result,
				CSS_PROP_GRID_TEMPLATE_AREAS, 0,
				GRID_TEMPLATE_AREAS_NONE);
	}

	while ((token = parserutils_vector_peek(vector, *ctx)) != NULL &&
			token->type == CSS_TOKEN_STRING && error == CSS_OK) {
		parserutils_vector_iterate(vector, ctx);
		error = grid_areas_add_row(&areas, token->idata);
		consumeWhitespace(vector, ctx);
	}

	if (error == CSS_OK && areas.rows == 0)
		error = CSS_INVALID;

	if (error == CSS_OK)
		error = grid_append_areas(c, result, &areas);

	grid_areas_free(&areas);
	if (error != CSS_OK)
		*ctx = orig_ctx;

	return error;
}

css_error css__parse_grid_auto_flow(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	int32_t orig_ctx = *ctx;
	const css_token *token;
	enum flag_value flag_value;
	bool row = false, column = false, dense = false;
	uint32_t value;

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	flag_value = get_css_flag_value(c, token);
	if (flag_value != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		return css_stylesheet_style_flag_value(result, flag_value,
				CSS_PROP_GRID_AUTO_FLOW);
	}

	while ((token = parserutils_vector_peek(vector, *ctx)) != NULL &&
			token->type == CSS_TOKEN_IDENT) {
		if (grid_is_ident(c, token, ROW) && !row && !column)
			row = true;
		else if (grid_is_ident(c, token, COLUMN) && !row && !column)
			column = true;
		else if (grid_is_ident(c, token, DENSE) && !dense)
			dense = true;
		else
			break;

		parserutils_vector_iterate(vector, ctx);
		consumeWhitespace(vector, ctx);
	}

	if (!row && !column && !dense) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}

	if (column)
		value = dense ? GRID_AUTO_FLOW_COLUMN_DENSE :
				GRID_AUTO_FLOW_COLUMN;
	else
		value = dense ? GRID_AUTO_FLOW_ROW_DENSE : GRID_AUTO_FLOW_ROW;

	return css__stylesheet_style_appendOPV(result,
			CSS_PROP_GRID_AUTO_FLOW, 0, value);
}

/**
 * Parse a grid line: auto, a name, an integer with an optional name, or
 * span with an integer, a name or both
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx     Pointer to vector iteration context
 * \param line    Updated to the line
 * \return CSS_OK on success, appropriate error otherwise
 */
static css_error grid_parse_line(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		struct grid_line *line)
{
	int32_t orig_ctx = *ctx;
	const css_token *token;
	const css_token *name = NULL;
	bool span = false, have_integer = false;
	int32_t integer = 0;
	css_error error;

	line->value = GRID_LINE_AUTO;
	line->integer = 0;
	line->name = 0;

	token = parserutils_vector_peek(vector, *ctx);
	if (grid_is_ident(c, token, AUTO)) {
		parserutils_vector_iterate(vector, ctx);
		consumeWhitespace(vector, ctx);
		return CSS_OK;
	}

	while (true) {
		token = parserutils_vector_peek(vector, *ctx);

		if (grid_is_ident(c, token, SPAN) && !span) {
			span = true;
		} else if (!have_integer && grid_parse_integer(token,
				&integer)) {
			have_integer = true;
		} else if (name == NULL && grid_is_custom_ident(c, token)) {
			name = token;
		} else {
			break;
		}

		parserutils_vector_iterate(vector, ctx);
		consumeWhitespace(vector, ctx);
	}

	/* span must be first or last; an integer may not be zero, or
	 * negative in a span */
	if ((!have_integer && name == NULL) ||
			(have_integer && integer == 0) ||
			(span && integer < 0)) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}

	if (span) {
		line->value = GRID_LINE_SPAN;
		line->integer = have_integer ? integer : 1;
	} else {
		line->value = GRID_LINE_SET;
		line->integer = integer;
	}

	if (name != NULL) {
		error = grid_add_name(c, name, &line->name);
		if (error != CSS_OK) {
			*ctx = orig_ctx;
			return error;
		}
		line->value |= GRID_LINE_NAMED;
	}

	return CSS_OK;
}

/**
 * Append a grid line to a style
 */
static css_error grid_append_line(css_style *result,
		enum css_properties_e prop, const struct grid_line *line)
{
	css_error error;

	error = css__stylesheet_style_appendOPV(result, prop, 0, line->value);
	if (error != CSS_OK || line->value == GRID_LINE_AUTO)
		return error;

	error = css__stylesheet_style_append(result, line->integer);
	if (error != CSS_OK || (line->value & GRID_LINE_NAMED) == 0)
		return error;

	return css__stylesheet_style_append(result, line->name);
}

/**
 * Parse one of grid-row-start, grid-row-end, grid-column-start and
 * grid-column-end
 */
static css_error grid_parse_line_property(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result, enum css_properties_e prop)
{
	const css_token *token;
	enum flag_value flag_value;
	struct grid_line line;
	css_error error;

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	flag_value = get_css_flag_value(c, token);
	if (flag_value != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		return css_stylesheet_style_flag_value(result, flag_value,
				prop);
	}

	error = grid_parse_line(c, vector, ctx, &line);
	if (error != CSS_OK)
		return error;

	return grid_append_line(result, prop, &line);
}

css_error css__parse_grid_column_start(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return grid_parse_line_property(c, vector, ctx, result,
			CSS_PROP_GRID_COLUMN_START);
}

css_error css__parse_grid_column_end(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return grid_parse_line_property(c, vector, ctx, result,
			CSS_PROP_GRID_COLUMN_END);
}

css_error css__parse_grid_row_start(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return grid_parse_line_property(c, vector, ctx, result,
			CSS_PROP_GRID_ROW_START);
}

css_error css__parse_grid_row_end(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return grid_parse_line_property(c, vector, ctx, result,
			CSS_PROP_GRID_ROW_END);
}

/**
 * Get the line an omitted line of a shorthand takes from another
 *
 * A line which is only a name is copied; anything else gives auto.
 */
static struct grid_line grid_line_default(const struct grid_line *from)
{
	struct grid_line line = { GRID_LINE_AUTO, 0, 0 };

	if (from->value == (GRID_LINE_SET | GRID_LINE_NAMED) &&
			from->integer == 0)
		line = *from;

	return line;
}

/**
 * Parse grid-row, grid-column or grid-area
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx     Pointer to vector iteration context
 * \param result  Pointer to location to receive resulting style
 * \param props   The longhands, in the order their values are given
 * \param n       The number of longhands, 2 or 4
 * \return CSS_OK on success, appropriate error otherwise
 */
static css_error grid_parse_lines_shorthand(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result, const enum css_properties_e *props,
		int n)
{
	int32_t orig_ctx = *ctx;
	struct grid_line lines[4];
	const css_token *token;
	enum flag_value flag_value;
	css_error error = CSS_OK;
	uint32_t used = result->used;
	int given = 0;
	int i;

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	flag_value = get_css_flag_value(c, token);
	if (flag_value != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		for (i = 0; i < n && error == CSS_OK; i++)
			error = css_stylesheet_style_flag_value(result,
					flag_value, props[i]);
		return error;
	}

	while (given < n) {
		error = grid_parse_line(c, vector, ctx, &lines[given]);
		if (error != CSS_OK) {
			*ctx = orig_ctx;
			return error;
		}
		given++;

		consumeWhitespace(vector, ctx);
		token = parserutils_vector_peek(vector, *ctx);
		if (tokenIsChar(token, '/') == false)
			break;

		parserutils_vector_iterate(vector, ctx);
		consumeWhitespace(vector, ctx);
	}

	/* The omitted lines: for grid-area, the column start copies the
	 * row start, and each end copies its start; for grid-row and
	 * grid-column, the end copies the start */
	for (i = given; i < n; i++)
		lines[i] = grid_line_default(&lines[i == 3 ? 1 : 0]);

	for (i = 0; i < n && error == CSS_OK; i++)
		error = grid_append_line(result, props[i], &lines[i]);

	if (error != CSS_OK) {
		result->used = used;
		*ctx = orig_ctx;
	}

	return error;
}

css_error css__parse_grid_column(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	static const enum css_properties_e props[] = {
		CSS_PROP_GRID_COLUMN_START, CSS_PROP_GRID_COLUMN_END
	};

	return grid_parse_lines_shorthand(c, vector, ctx, result, props, 2);
}

css_error css__parse_grid_row(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	static const enum css_properties_e props[] = {
		CSS_PROP_GRID_ROW_START, CSS_PROP_GRID_ROW_END
	};

	return grid_parse_lines_shorthand(c, vector, ctx, result, props, 2);
}

css_error css__parse_grid_area(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	static const enum css_properties_e props[] = {
		CSS_PROP_GRID_ROW_START, CSS_PROP_GRID_COLUMN_START,
		CSS_PROP_GRID_ROW_END, CSS_PROP_GRID_COLUMN_END
	};

	return grid_parse_lines_shorthand(c, vector, ctx, result, props, 4);
}

/**
 * Parse grid-template
 *
 * This is none, rows and columns separated by a slash, or strings of
 * grid-template-areas, each with an optional row size and line names,
 * then optionally a slash and columns.
 */
css_error css__parse_grid_template(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	struct grid_track_list rows = { NULL, 0, 0, 0, false };
	struct grid_track_list columns = { NULL, 0, 0, 0, false };
	struct grid_areas areas = { NULL, 0, 0, 0 };
	int32_t orig_ctx = *ctx;
	uint32_t used = result->used;
	const css_token *token;
	enum flag_value flag_value;
	css_error error = CSS_OK;

	token = parserutils_vector_peek(vector, *ctx);
	if (token == NULL)
		return CSS_INVALID;

	flag_value = get_css_flag_value(c, token);
	if (flag_value != FLAG_VALUE__NONE) {
		parserutils_vector_iterate(vector, ctx);
		error = css_stylesheet_style_flag_value(result, flag_value,
				CSS_PROP_GRID_TEMPLATE_ROWS);
		if (error == CSS_OK)
			error = css_stylesheet_style_flag_value(result,
					flag_value,
					CSS_PROP_GRID_TEMPLATE_COLUMNS);
		if (error == CSS_OK)
			error = css_stylesheet_style_flag_value(result,
					flag_value,
					CSS_PROP_GRID_TEMPLATE_AREAS);
		return error;
	}

	if (grid_is_ident(c, token, NONE)) {
		parserutils_vector_iterate(vector, ctx);
		error = css__stylesheet_style_appendOPV(result,
				CSS_PROP_GRID_TEMPLATE_ROWS, 0,
				GRID_TEMPLATE_NONE);
		if (error == CSS_OK)
			error = css__stylesheet_style_appendOPV(result,
					CSS_PROP_GRID_TEMPLATE_COLUMNS, 0,
					GRID_TEMPLATE_NONE);
		if (error == CSS_OK)
			error = css__stylesheet_style_appendOPV(result,
					CSS_PROP_GRID_TEMPLATE_AREAS, 0,
					GRID_TEMPLATE_AREAS_NONE);
		goto cleanup;
	}

	/* Look for a string, which gives the form with areas */
	while (true) {
		consumeWhitespace(vector, ctx);
		token = parserutils_vector_peek(vector, *ctx);
		if (tokenIsChar(token, '[')) {
			error = grid_parse_line_names(c, vector, ctx, &rows);
			if (error != CSS_OK)
				goto cleanup;
			continue;
		}
		break;
	}

	if (token != NULL && token->type == CSS_TOKEN_STRING) {
		while (token != NULL && token->type == CSS_TOKEN_STRING) {
			struct grid_track track;

			parserutils_vector_iterate(vector, ctx);
			error = grid_areas_add_row(&areas, token->idata);
			if (error != CSS_OK)
				goto cleanup;

			consumeWhitespace(vector, ctx);
			if (grid_parse_track_size(c, vector, ctx,
					&track) == false) {
				memset(&track, 0, sizeof(track));
				track.kind = CSS_COMPUTED_GRID_TRACK_SIZE;
				track.min_type = CSS_COMPUTED_GRID_SIZING_AUTO;
				track.max_type = CSS_COMPUTED_GRID_SIZING_AUTO;
				track.min_unit = UNIT_PX;
				track.max_unit = UNIT_PX;
			}

			error = grid_list_add(&rows, &track);
			if (error != CSS_OK)
				goto cleanup;

			consumeWhitespace(vector, ctx);
			token = parserutils_vector_peek(vector, *ctx);
			if (tokenIsChar(token, '[')) {
				error = grid_parse_line_names(c, vector, ctx,
						&rows);
				if (error != CSS_OK)
					goto cleanup;

				consumeWhitespace(vector, ctx);
				token = parserutils_vector_peek(vector, *ctx);
			}

			/* Names before the next string */
			if (tokenIsChar(token, '[')) {
				error = grid_parse_line_names(c, vector, ctx,
						&rows);
				if (error != CSS_OK)
					goto cleanup;

				consumeWhitespace(vector, ctx);
				token = parserutils_vector_peek(vector, *ctx);
			}
		}
	} else {
		error = grid_parse_track_list(c, vector, ctx, true, &rows);
		if (error != CSS_OK)
			goto cleanup;
	}

	consumeWhitespace(vector, ctx);
	token = parserutils_vector_peek(vector, *ctx);
	if (tokenIsChar(token, '/')) {
		parserutils_vector_iterate(vector, ctx);
		error = grid_parse_track_list(c, vector, ctx, true, &columns);
		if (error != CSS_OK)
			goto cleanup;
	} else if (areas.rows == 0) {
		/* Rows alone need columns */
		error = CSS_INVALID;
		goto cleanup;
	}

	error = grid_append_track_list(result, CSS_PROP_GRID_TEMPLATE_ROWS,
			GRID_TEMPLATE_SET, &rows);
	if (error != CSS_OK)
		goto cleanup;

	if (columns.sizes > 0)
		error = grid_append_track_list(result,
				CSS_PROP_GRID_TEMPLATE_COLUMNS,
				GRID_TEMPLATE_SET, &columns);
	else
		error = css__stylesheet_style_appendOPV(result,
				CSS_PROP_GRID_TEMPLATE_COLUMNS, 0,
				GRID_TEMPLATE_NONE);
	if (error != CSS_OK)
		goto cleanup;

	if (areas.rows > 0)
		error = grid_append_areas(c, result, &areas);
	else
		error = css__stylesheet_style_appendOPV(result,
				CSS_PROP_GRID_TEMPLATE_AREAS, 0,
				GRID_TEMPLATE_AREAS_NONE);

cleanup:
	free(rows.entries);
	free(columns.entries);
	grid_areas_free(&areas);

	if (error != CSS_OK) {
		result->used = used;
		*ctx = orig_ctx;
	}

	return error;
}

/**
 * Parse a shorthand of two properties, where one value sets both
 *
 * \param c       Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx     Pointer to vector iteration context
 * \param result  Pointer to location to receive resulting style
 * \param first   Parser of the first longhand
 * \param second  Parser of the second longhand
 * \return CSS_OK on success, appropriate error otherwise
 */
static css_error grid_parse_pair(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result, css_prop_handler first,
		css_prop_handler second)
{
	int32_t orig_ctx = *ctx;
	uint32_t used = result->used;
	css_error error;

	error = first(c, vector, ctx, result);
	if (error != CSS_OK)
		return error;

	consumeWhitespace(vector, ctx);
	if (grid_is_end(parserutils_vector_peek(vector, *ctx))) {
		/* The second takes the same value */
		int32_t again = orig_ctx;

		error = second(c, vector, &again, result);
	} else {
		error = second(c, vector, ctx, result);
	}

	if (error != CSS_OK) {
		result->used = used;
		*ctx = orig_ctx;
	}

	return error;
}

css_error css__parse_gap(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return grid_parse_pair(c, vector, ctx, result,
			css__parse_row_gap, css__parse_column_gap);
}

css_error css__parse_grid_gap(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return css__parse_gap(c, vector, ctx, result);
}

css_error css__parse_place_content(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return grid_parse_pair(c, vector, ctx, result,
			css__parse_align_content, css__parse_justify_content);
}

css_error css__parse_place_items(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return grid_parse_pair(c, vector, ctx, result,
			css__parse_align_items, css__parse_justify_items);
}

css_error css__parse_place_self(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	return grid_parse_pair(c, vector, ctx, result,
			css__parse_align_self, css__parse_justify_self);
}
