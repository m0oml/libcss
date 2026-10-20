/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2026 Trevor Cockayne
 */

/** \file
 * Custom properties (--name: value) and var() substitution.
 *
 * During selection the winning custom property declarations and the
 * properties whose winning declaration uses var() are recorded in the
 * partial computed style.  When the style is composed with its parent the
 * custom properties are inherited and resolved, the var() references are
 * substituted and the resulting values are parsed and applied.  The root
 * element, which is not composed, is resolved at the end of selection.
 *
 * A value which cannot be substituted or parsed makes the property behave
 * as unset, as CSS requires for values invalid at computed-value time.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <parserutils/utils/buffer.h>

#include "stylesheet.h"
#include "select/computed.h"
#include "select/dispatch.h"
#include "select/vars.h"
#include "utils/parserutilserror.h"
#include "utils/utils.h"

/** Limit on nesting of var() fallbacks and custom property references */
#define VARS_MAX_DEPTH 32

/** Number of parsed values a var() source keeps */
#define VARS_CACHE_SIZE 256

/**
 * A parsed value, cached by its source
 */
struct var_parse {
	lwc_string *name;		/**< Property name */
	lwc_string *value;		/**< Substituted value */
	css_stylesheet *sheet;		/**< Parsed declaration */
};

/**
 * Where declarations using var() came from
 *
 * Substituted values are parsed as if they were part of the stylesheet
 * the declaration came from, so URLs resolve against it.  The source
 * outlives the stylesheet if computed styles still refer to it.
 */
struct css_var_source {
	uint32_t refcount;
	char *url;
	css_language_level level;
	bool allow_quirks;
	css_url_resolution_fn resolve;
	void *resolve_pw;
	css_color_resolution_fn color;
	void *color_pw;
	css_font_resolution_fn font;
	void *font_pw;

	struct var_parse cache[VARS_CACHE_SIZE];
	uint32_t n_cache;
};

/**
 * A custom property declaration found during selection
 */
struct select_custom {
	lwc_string *name;
	lwc_string *value;
	prop_state state;
};

/**
 * Custom property data for one pseudo element during selection
 */
struct select_vars_pseudo {
	struct select_custom *custom;
	uint32_t n_custom;
	uint32_t alloc_custom;

	/** Declarations using var(), valid where the property's
	 * explicit_default is FLAG_VALUE_VAR */
	css_var_pending *pending;
};

/**
 * Custom property data for a selection
 */
struct css_select_vars {
	struct select_vars_pseudo pseudo[CSS_PSEUDO_ELEMENT_COUNT];
};


/******************************************************************************
 * Sources                                                                    *
 ******************************************************************************/

static void var_source_clear_cache(struct css_var_source *source)
{
	uint32_t i;

	for (i = 0; i < source->n_cache; i++) {
		lwc_string_unref(source->cache[i].name);
		lwc_string_unref(source->cache[i].value);
		if (source->cache[i].sheet != NULL)
			css_stylesheet_destroy(source->cache[i].sheet);
	}
	source->n_cache = 0;
}

/* Internally exported function, documented in select/vars.h */
void css__var_source_unref(struct css_var_source *source)
{
	if (source == NULL || --source->refcount > 0)
		return;

	var_source_clear_cache(source);
	free(source->url);
	free(source);
}

static struct css_var_source *var_source_ref(struct css_var_source *source)
{
	source->refcount++;
	return source;
}

/**
 * Get the var() source for a stylesheet, creating it if needed
 */
static css_error var_source_for_sheet(css_stylesheet *sheet,
		struct css_var_source **result)
{
	struct css_var_source *source = sheet->var_source;

	if (source == NULL) {
		source = calloc(1, sizeof(*source));
		if (source == NULL)
			return CSS_NOMEM;

		if (sheet->url != NULL) {
			source->url = strdup(sheet->url);
			if (source->url == NULL) {
				free(source);
				return CSS_NOMEM;
			}
		}
		source->refcount = 1;
		source->level = sheet->level;
		source->allow_quirks = sheet->quirks_allowed;
		source->resolve = sheet->resolve;
		source->resolve_pw = sheet->resolve_pw;
		source->color = sheet->color;
		source->color_pw = sheet->color_pw;
		source->font = sheet->font;
		source->font_pw = sheet->font_pw;

		sheet->var_source = source;
	}

	*result = source;
	return CSS_OK;
}

/**
 * Parse a declaration as if it was in its source stylesheet
 *
 * \param source  Source of the declaration
 * \param name    Name of the property
 * \param value   Value, with var() substituted
 * \param style   Updated to the parsed bytecode, or NULL if invalid.
 *                It remains owned by the source.
 * \return CSS_OK on success, appropriate error otherwise
 */
static css_error var_source_parse(struct css_var_source *source,
		lwc_string *name, lwc_string *value, const css_style **style)
{
	css_stylesheet_params params;
	css_stylesheet *sheet = NULL;
	struct var_parse *entry;
	css_error error;
	uint32_t i;

	*style = NULL;

	for (i = 0; i < source->n_cache; i++) {
		entry = &source->cache[i];
		if (entry->name == name && entry->value == value)
			goto found;
	}

	memset(&params, 0, sizeof(params));
	params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
	params.level = source->level;
	params.charset = "UTF-8";
	params.url = source->url != NULL ? source->url : "";
	params.allow_quirks = source->allow_quirks;
	params.inline_style = true;
	params.resolve = source->resolve;
	params.resolve_pw = source->resolve_pw;
	params.color = source->color;
	params.color_pw = source->color_pw;
	params.font = source->font;
	params.font_pw = source->font_pw;

	error = css_stylesheet_create(&params, &sheet);
	if (error != CSS_OK)
		return error;

	error = css_stylesheet_append_data(sheet,
			(const uint8_t *) lwc_string_data(name),
			lwc_string_length(name));
	if (error == CSS_OK || error == CSS_NEEDDATA)
		error = css_stylesheet_append_data(sheet,
				(const uint8_t *) ":", 1);
	if (error == CSS_OK || error == CSS_NEEDDATA)
		error = css_stylesheet_append_data(sheet,
				(const uint8_t *) lwc_string_data(value),
				lwc_string_length(value));
	if (error == CSS_OK || error == CSS_NEEDDATA)
		error = css_stylesheet_data_done(sheet);
	if (error != CSS_OK) {
		/* Invalid values are cached too, without a sheet */
		css_stylesheet_destroy(sheet);
		sheet = NULL;
		if (error == CSS_NOMEM)
			return error;
	}

	if (source->n_cache == VARS_CACHE_SIZE)
		var_source_clear_cache(source);

	entry = &source->cache[source->n_cache++];
	entry->name = lwc_string_ref(name);
	entry->value = lwc_string_ref(value);
	entry->sheet = sheet;

found:
	if (entry->sheet != NULL && entry->sheet->rule_list != NULL &&
			entry->sheet->rule_list->type == CSS_RULE_SELECTOR) {
		*style = ((css_rule_selector *) entry->sheet->rule_list)->style;
	}

	return CSS_OK;
}


/******************************************************************************
 * Computed custom properties                                                 *
 ******************************************************************************/

static css_computed_vars *computed_vars_ref(css_computed_vars *vars)
{
	if (vars != NULL)
		vars->refcount++;
	return vars;
}

/* Internally exported function, documented in select/vars.h */
void css__computed_vars_unref(css_computed_vars *vars)
{
	uint32_t i;

	if (vars == NULL || --vars->refcount > 0)
		return;

	for (i = 0; i < vars->n_custom; i++) {
		lwc_string_unref(vars->custom[i].name);
		lwc_string_unref(vars->custom[i].value);
	}
	free(vars->custom);

	for (i = 0; i < vars->n_pending; i++) {
		lwc_string_unref(vars->pending[i].name);
		lwc_string_unref(vars->pending[i].value);
		css__var_source_unref(vars->pending[i].source);
	}
	free(vars->pending);

	free(vars);
}

/* Internally exported function, documented in select/vars.h */
bool css__computed_vars_equal(const css_computed_vars *a,
		const css_computed_vars *b)
{
	uint32_t i;

	if (a == b)
		return true;
	if (a == NULL || b == NULL)
		return false;
	if (a->n_custom != b->n_custom || a->n_pending != b->n_pending)
		return false;

	/* Interned strings are equal only if they are the same string */
	for (i = 0; i < a->n_custom; i++) {
		if (a->custom[i].name != b->custom[i].name ||
				a->custom[i].value != b->custom[i].value)
			return false;
	}

	for (i = 0; i < a->n_pending; i++) {
		if (a->pending[i].op != b->pending[i].op ||
				a->pending[i].name != b->pending[i].name ||
				a->pending[i].value != b->pending[i].value ||
				a->pending[i].source != b->pending[i].source)
			return false;
	}

	return true;
}

static int custom_compare(const void *a, const void *b)
{
	const css_custom_property *ca = a;
	const css_custom_property *cb = b;
	size_t la = lwc_string_length(ca->name);
	size_t lb = lwc_string_length(cb->name);
	int cmp = memcmp(lwc_string_data(ca->name), lwc_string_data(cb->name),
			la < lb ? la : lb);

	if (cmp != 0)
		return cmp;
	return la < lb ? -1 : (la > lb ? 1 : 0);
}

/**
 * Find a custom property by name
 */
static const css_custom_property *custom_find(const css_custom_property *custom,
		uint32_t n_custom, const char *name, size_t len)
{
	uint32_t lo = 0;
	uint32_t hi = n_custom;

	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;
		size_t mlen = lwc_string_length(custom[mid].name);
		int cmp = memcmp(name, lwc_string_data(custom[mid].name),
				len < mlen ? len : mlen);

		if (cmp == 0)
			cmp = len < mlen ? -1 : (len > mlen ? 1 : 0);
		if (cmp == 0)
			return &custom[mid];
		if (cmp < 0)
			hi = mid;
		else
			lo = mid + 1;
	}

	return NULL;
}


/******************************************************************************
 * var() substitution                                                         *
 ******************************************************************************/

/**
 * Look up the value of a custom property for substitution
 *
 * \param pw     Client data
 * \param name   Name of the custom property
 * \param len    Length of name
 * \param value  Updated to the value, or NULL if it has none
 * \return CSS_OK on success, appropriate error otherwise
 */
typedef css_error (*var_lookup_fn)(void *pw, const char *name, size_t len,
		lwc_string **value);

static inline bool is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static inline bool is_name_char(char c)
{
	return c == '-' || c == '_' || (c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			(unsigned char) c >= 0x80 || c == '\\';
}

/**
 * Skip a quoted string
 *
 * \return Index after the closing quote, or the end of the text
 */
static size_t skip_string(const char *text, size_t len, size_t i)
{
	char quote = text[i++];

	while (i < len && text[i] != quote) {
		if (text[i] == '\\' && i + 1 < len)
			i++;
		i++;
	}

	return i < len ? i + 1 : len;
}

/**
 * Find the end of a var() argument
 *
 * \return Index of the ',' or ')' ending the argument, or len if none
 */
static size_t find_argument_end(const char *text, size_t len, size_t i,
		bool stop_at_comma)
{
	int depth = 0;

	while (i < len) {
		char c = text[i];

		if (c == '"' || c == '\'') {
			i = skip_string(text, len, i);
			continue;
		}
		if (c == '(' || c == '[' || c == '{') {
			depth++;
		} else if (c == ')' || c == ']' || c == '}') {
			if (depth == 0)
				return i;
			depth--;
		} else if (c == ',' && depth == 0 && stop_at_comma) {
			return i;
		}
		i++;
	}

	return len;
}

static css_error buffer_append(parserutils_buffer *buf, const char *data,
		size_t len)
{
	return css_error_from_parserutils_error(parserutils_buffer_append(buf,
			(const uint8_t *) data, len));
}

/**
 * Substitute var() references in a value
 *
 * \param text    The value
 * \param len     Length of the value
 * \param lookup  Function to find custom property values
 * \param pw      Client data for lookup
 * \param depth   Nesting depth of fallbacks
 * \param out     Buffer to append the substituted value to
 * \return CSS_OK on success,
 *         CSS_INVALID if a var() has no value and no fallback,
 *         appropriate error otherwise
 */
static css_error substitute(const char *text, size_t len,
		var_lookup_fn lookup, void *pw, int depth,
		parserutils_buffer *out)
{
	size_t start = 0;
	size_t i = 0;
	css_error error;

	if (depth > VARS_MAX_DEPTH)
		return CSS_INVALID;

	while (i < len) {
		size_t name_start, name_end, end;
		lwc_string *value = NULL;

		if (text[i] == '"' || text[i] == '\'') {
			i = skip_string(text, len, i);
			continue;
		}

		if (i + 4 > len || strncasecmp(text + i, "var(", 4) != 0 ||
				(i > 0 && is_name_char(text[i - 1]))) {
			i++;
			continue;
		}

		/* Copy the text before the var() */
		error = buffer_append(out, text + start, i - start);
		if (error != CSS_OK)
			return error;

		/* var( S* name S* [, fallback]? ) */
		name_start = i + 4;
		while (name_start < len && is_space(text[name_start]))
			name_start++;
		name_end = name_start;
		while (name_end < len && is_name_char(text[name_end]))
			name_end++;
		if (name_end - name_start < 3 || text[name_start] != '-' ||
				text[name_start + 1] != '-')
			return CSS_INVALID;

		end = name_end;
		while (end < len && is_space(text[end]))
			end++;
		if (end == len || (text[end] != ',' && text[end] != ')'))
			return CSS_INVALID;

		error = lookup(pw, text + name_start, name_end - name_start,
				&value);
		if (error != CSS_OK)
			return error;

		if (text[end] == ',') {
			size_t fallback = end + 1;

			end = find_argument_end(text, len, fallback, false);
			if (end == len)
				return CSS_INVALID;

			if (value == NULL) {
				error = substitute(text + fallback,
						end - fallback, lookup, pw,
						depth + 1, out);
				if (error != CSS_OK)
					return error;
			}
		} else if (value == NULL) {
			return CSS_INVALID;
		}

		if (value != NULL) {
			error = buffer_append(out, lwc_string_data(value),
					lwc_string_length(value));
			if (error != CSS_OK)
				return error;
		}

		/* Continue after the closing bracket */
		i = start = end + 1;
	}

	return buffer_append(out, text + start, len - start);
}

/**
 * Substitute var() references in a value, giving an interned string
 */
static css_error substitute_string(lwc_string *value, var_lookup_fn lookup,
		void *pw, lwc_string **result)
{
	parserutils_buffer *buf;
	css_error error;

	if (parserutils_buffer_create(&buf) != PARSERUTILS_OK)
		return CSS_NOMEM;

	error = substitute(lwc_string_data(value), lwc_string_length(value),
			lookup, pw, 0, buf);
	if (error == CSS_OK) {
		error = css_error_from_lwc_error(lwc_intern_string(
				(const char *) buf->data, buf->length,
				result));
	}

	parserutils_buffer_destroy(buf);
	return error;
}

/** Contains a var() reference */
static bool uses_var(lwc_string *value)
{
	const char *data = lwc_string_data(value);
	size_t len = lwc_string_length(value);
	size_t i;

	for (i = 0; i + 4 <= len; i++) {
		if (strncasecmp(data + i, "var(", 4) == 0)
			return true;
	}

	return false;
}

static bool is_keyword(lwc_string *value, const char *keyword)
{
	size_t len = strlen(keyword);

	return lwc_string_length(value) == len &&
			strncasecmp(lwc_string_data(value), keyword, len) == 0;
}


/******************************************************************************
 * Resolving custom properties                                                *
 ******************************************************************************/

enum resolve_state {
	RESOLVED,	/**< Value is final, or NULL if invalid */
	UNRESOLVED,	/**< Value may contain var() */
	RESOLVING	/**< Being resolved, so a reference is a cycle */
};

struct resolve_entry {
	lwc_string *name;
	lwc_string *value;
	enum resolve_state state;
};

struct resolve_ctx {
	struct resolve_entry *entries;	/**< Sorted by name */
	uint32_t n_entries;
	int depth;
};

static css_error resolve_entry(struct resolve_ctx *ctx,
		struct resolve_entry *entry);

/**
 * Find an entry by name in the sorted entries
 */
static struct resolve_entry *resolve_find(struct resolve_ctx *ctx,
		const char *name, size_t len)
{
	uint32_t lo = 0;
	uint32_t hi = ctx->n_entries;

	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;
		size_t mlen = lwc_string_length(ctx->entries[mid].name);
		int cmp = memcmp(name, lwc_string_data(ctx->entries[mid].name),
				len < mlen ? len : mlen);

		if (cmp == 0)
			cmp = len < mlen ? -1 : (len > mlen ? 1 : 0);
		if (cmp == 0)
			return &ctx->entries[mid];
		if (cmp < 0)
			hi = mid;
		else
			lo = mid + 1;
	}

	return NULL;
}

static css_error resolve_lookup(void *pw, const char *name, size_t len,
		lwc_string **value)
{
	struct resolve_ctx *ctx = pw;
	struct resolve_entry *entry;
	css_error error;

	*value = NULL;

	entry = resolve_find(ctx, name, len);
	if (entry == NULL)
		return CSS_OK;

	error = resolve_entry(ctx, entry);
	if (error != CSS_OK)
		return error;

	*value = entry->value;
	return CSS_OK;
}

static css_error resolve_entry(struct resolve_ctx *ctx,
		struct resolve_entry *entry)
{
	lwc_string *result = NULL;
	css_error error;

	if (entry->state == RESOLVED)
		return CSS_OK;

	if (entry->state == RESOLVING || ctx->depth > VARS_MAX_DEPTH) {
		/* A cycle makes the property invalid */
		lwc_string_unref(entry->value);
		entry->value = NULL;
		entry->state = RESOLVED;
		return CSS_OK;
	}

	entry->state = RESOLVING;
	ctx->depth++;
	error = substitute_string(entry->value, resolve_lookup, ctx, &result);
	ctx->depth--;

	if (entry->state == RESOLVED) {
		/* Found to be in a cycle while resolving */
		lwc_string_unref(result);
		return error == CSS_INVALID ? CSS_OK : error;
	}

	lwc_string_unref(entry->value);
	entry->value = NULL;
	entry->state = RESOLVED;

	if (error == CSS_INVALID)
		return CSS_OK;
	if (error != CSS_OK)
		return error;

	entry->value = result;
	return CSS_OK;
}

static int resolve_compare(const void *a, const void *b)
{
	const struct resolve_entry *ea = a;
	const struct resolve_entry *eb = b;
	css_custom_property ca = { ea->name, NULL };
	css_custom_property cb = { eb->name, NULL };

	return custom_compare(&ca, &cb);
}

/**
 * Work out the custom properties of an element
 *
 * \param inherited  Resolved custom properties of the parent, or NULL
 * \param own        The element's own custom property declarations
 * \param n_own      Number of declarations
 * \param result     Updated to the resolved custom properties, or NULL
 * \return CSS_OK on success, appropriate error otherwise
 */
static css_error resolve_custom(const css_computed_vars *inherited,
		const css_custom_property *own, uint32_t n_own,
		css_computed_vars **result)
{
	struct resolve_ctx ctx;
	css_computed_vars *vars;
	uint32_t n_inherited = inherited != NULL ? inherited->n_custom : 0;
	uint32_t i, j, n;
	css_error error = CSS_OK;

	*result = NULL;

	ctx.entries = calloc(n_inherited + n_own + 1, sizeof(*ctx.entries));
	if (ctx.entries == NULL)
		return CSS_NOMEM;
	ctx.n_entries = 0;
	ctx.depth = 0;

	for (i = 0; i < n_inherited; i++) {
		struct resolve_entry *e = &ctx.entries[ctx.n_entries++];

		e->name = lwc_string_ref(inherited->custom[i].name);
		e->value = lwc_string_ref(inherited->custom[i].value);
		e->state = RESOLVED;
	}

	/* Own declarations replace inherited values */
	for (i = 0; i < n_own; i++) {
		struct resolve_entry *e = NULL;

		for (j = 0; j < n_inherited; j++) {
			if (ctx.entries[j].name == own[i].name) {
				e = &ctx.entries[j];
				break;
			}
		}

		if (is_keyword(own[i].value, "inherit") ||
				is_keyword(own[i].value, "unset")) {
			/* Custom properties inherit anyway */
			continue;
		}

		if (e == NULL) {
			e = &ctx.entries[ctx.n_entries++];
			e->name = lwc_string_ref(own[i].name);
		} else {
			lwc_string_unref(e->value);
		}

		if (is_keyword(own[i].value, "initial")) {
			/* The initial value is the guaranteed-invalid value */
			e->value = NULL;
			e->state = RESOLVED;
		} else {
			e->value = lwc_string_ref(own[i].value);
			e->state = uses_var(own[i].value) ?
					UNRESOLVED : RESOLVED;
		}
	}

	qsort(ctx.entries, ctx.n_entries, sizeof(*ctx.entries),
			resolve_compare);

	for (i = 0; i < ctx.n_entries && error == CSS_OK; i++)
		error = resolve_entry(&ctx, &ctx.entries[i]);

	/* Build the result from the valid properties */
	n = 0;
	for (i = 0; i < ctx.n_entries; i++) {
		if (ctx.entries[i].value != NULL)
			n++;
	}

	vars = NULL;
	if (error == CSS_OK && n > 0) {
		vars = calloc(1, sizeof(*vars));
		if (vars != NULL)
			vars->custom = malloc(n * sizeof(*vars->custom));
		if (vars == NULL || vars->custom == NULL) {
			free(vars);
			vars = NULL;
			error = CSS_NOMEM;
		}
	}

	for (i = 0; i < ctx.n_entries; i++) {
		struct resolve_entry *e = &ctx.entries[i];

		if (vars != NULL && e->value != NULL) {
			vars->custom[vars->n_custom].name = e->name;
			vars->custom[vars->n_custom].value = e->value;
			vars->n_custom++;
		} else {
			lwc_string_unref(e->name);
			lwc_string_unref(e->value);
		}
	}
	free(ctx.entries);

	if (vars != NULL) {
		vars->refcount = 1;
		*result = vars;
	}

	return error;
}


/******************************************************************************
 * Applying substituted values                                                *
 ******************************************************************************/

static css_error map_lookup(void *pw, const char *name, size_t len,
		lwc_string **value)
{
	const css_computed_vars *vars = pw;
	const css_custom_property *prop = NULL;

	if (vars != NULL)
		prop = custom_find(vars->custom, vars->n_custom, name, len);

	*value = prop != NULL ? prop->value : NULL;
	return CSS_OK;
}

/**
 * Make a selection state for applying values to a style
 */
static void apply_state_init(css_select_state *state,
		css_computed_style *style,
		css_select_handler *handler, void *pw)
{
	memset(state, 0, sizeof(*state));
	state->computed = style;
	state->current_pseudo = CSS_PSEUDO_ELEMENT_NONE;
	state->current_origin = CSS_ORIGIN_AUTHOR;
	state->handler = handler;
	state->pw = pw;
}

/**
 * Apply a property waiting for var() substitution
 *
 * \param target    Style to set the property in
 * \param scratch   State used to parse into a scratch style
 * \param apply     State used to set properties in \a target
 * \param vars      Custom properties to substitute
 * \param pending   The property and its value
 * \param root      Whether the element is the root, which cannot inherit
 * \return CSS_OK on success, appropriate error otherwise
 */
static css_error apply_pending(css_computed_style *target,
		css_select_state *scratch, css_select_state *apply,
		const css_computed_vars *vars, const css_var_pending *pending,
		bool root)
{
	enum flag_value flag = FLAG_VALUE_UNSET;
	opcode_t op = pending->op;
	lwc_string *value = NULL;
	const css_style *parsed = NULL;
	css_error error;

	error = substitute_string(pending->value, map_lookup, (void *) vars,
			&value);
	if (error == CSS_OK) {
		error = var_source_parse(pending->source, pending->name,
				value, &parsed);
		lwc_string_unref(value);
	}
	if (error != CSS_OK && error != CSS_INVALID)
		return error;

	if (parsed != NULL) {
		prop_state *ps = &scratch->props[op][CSS_PSEUDO_ELEMENT_NONE];

		memset(scratch->props, 0, sizeof(scratch->props));
		error = css__cascade_style(parsed, scratch);
		if (error != CSS_OK)
			return error;

		if (ps->set)
			flag = ps->explicit_default;
	}

	/* Anything else makes the property invalid at computed-value
	 * time, so it is unset */
	if (flag == FLAG_VALUE_REVERT || flag == FLAG_VALUE_VAR)
		flag = FLAG_VALUE_UNSET;
	if (flag == FLAG_VALUE_UNSET)
		flag = prop_dispatch[op].inherited ?
				FLAG_VALUE_INHERIT : FLAG_VALUE_INITIAL;
	if (flag == FLAG_VALUE_INHERIT && root)
		flag = FLAG_VALUE_INITIAL;

	/* The initial values of these depend on the client, which is not
	 * available when composing, but they are inherited anyway */
	if (flag == FLAG_VALUE_INITIAL && apply->handler == NULL &&
			(op == CSS_PROP_COLOR || op == CSS_PROP_FONT_FAMILY ||
			 op == CSS_PROP_QUOTES))
		flag = FLAG_VALUE_INHERIT;

	apply->computed = target;
	memset(&apply->props[op], 0, sizeof(apply->props[op]));

	switch (flag) {
	case FLAG_VALUE__NONE:
		return prop_dispatch[op].copy(scratch->computed, target);
	case FLAG_VALUE_INITIAL:
		return prop_dispatch[op].initial(apply);
	default:
	{
		css_style none = { NULL, 0, 0, NULL };

		return prop_dispatch[op].cascade(
				buildOPV(op, FLAG_INHERIT, 0), &none, apply);
	}
	}
}

/**
 * Apply all the properties waiting for var() substitution
 */
static css_error apply_all_pending(css_computed_style *target,
		const css_computed_vars *vars,
		const css_var_pending *pending, uint32_t n_pending,
		bool root, css_select_handler *handler, void *pw)
{
	css_select_state *states;
	css_computed_style *scratch = NULL;
	css_error error;
	uint32_t i;

	states = malloc(2 * sizeof(*states));
	if (states == NULL)
		return CSS_NOMEM;

	error = css__computed_style_create(&scratch, target->calc);
	if (error != CSS_OK) {
		free(states);
		return error;
	}

	apply_state_init(&states[0], scratch, handler, pw);
	apply_state_init(&states[1], target, handler, pw);

	for (i = 0; i < n_pending && error == CSS_OK; i++) {
		error = apply_pending(target, &states[0], &states[1], vars,
				&pending[i], root);
	}

	css_computed_style_destroy(scratch);
	free(states);

	return error;
}


/******************************************************************************
 * Selection                                                                  *
 ******************************************************************************/

/**
 * Get the selection data for the current pseudo element
 *
 * \return The data, or NULL if custom properties are not being recorded
 */
static css_error select_vars_pseudo(css_select_state *state,
		struct select_vars_pseudo **result)
{
	/* States made to apply values, rather than to select a node's
	 * style, do not record custom properties */
	if (state->node == NULL) {
		*result = NULL;
		return CSS_OK;
	}

	if (state->vars == NULL) {
		state->vars = calloc(1, sizeof(*state->vars));
		if (state->vars == NULL)
			return CSS_NOMEM;
	}

	*result = &state->vars->pseudo[state->current_pseudo];
	return CSS_OK;
}

/* Internally exported function, documented in select/vars.h */
css_error css__select_vars_custom(css_code_t opv, css_style *style,
		css_select_state *state)
{
	struct select_vars_pseudo *pseudo;
	struct select_custom *custom = NULL;
	lwc_string *name;
	lwc_string *value;
	css_error error;
	uint32_t i;

	css__stylesheet_string_get(style->sheet, style->bytecode[0], &name);
	css__stylesheet_string_get(style->sheet, style->bytecode[1], &value);
	advance_bytecode(style, 2 * sizeof(css_code_t));

	error = select_vars_pseudo(state, &pseudo);
	if (error != CSS_OK || pseudo == NULL)
		return error;

	for (i = 0; i < pseudo->n_custom; i++) {
		if (pseudo->custom[i].name == name) {
			custom = &pseudo->custom[i];
			break;
		}
	}

	if (custom == NULL) {
		if (pseudo->n_custom == pseudo->alloc_custom) {
			uint32_t alloc = pseudo->alloc_custom * 2 + 8;
			struct select_custom *temp;

			temp = realloc(pseudo->custom,
					alloc * sizeof(*pseudo->custom));
			if (temp == NULL)
				return CSS_NOMEM;
			pseudo->custom = temp;
			pseudo->alloc_custom = alloc;
		}

		custom = &pseudo->custom[pseudo->n_custom++];
		memset(custom, 0, sizeof(*custom));
		custom->name = lwc_string_ref(name);
	}

	if (css__outranks_state(&custom->state, isImportant(opv), state,
			FLAG_VALUE__NONE)) {
		lwc_string_unref(custom->value);
		custom->value = lwc_string_ref(value);
	}

	return CSS_OK;
}

/* Internally exported function, documented in select/vars.h */
css_error css__select_vars_pending(css_code_t opv, css_style *style,
		css_select_state *state)
{
	struct select_vars_pseudo *pseudo;
	struct css_var_source *source;
	css_var_pending *pending;
	opcode_t op = getOpcode(opv);
	lwc_string *name;
	lwc_string *value;
	css_error error;

	css__stylesheet_string_get(style->sheet, style->bytecode[0], &value);
	css__stylesheet_string_get(style->sheet, style->bytecode[1], &name);
	advance_bytecode(style, 2 * sizeof(css_code_t));

	error = select_vars_pseudo(state, &pseudo);
	if (error != CSS_OK || pseudo == NULL)
		return error;

	if (pseudo->pending == NULL) {
		pseudo->pending = calloc(CSS_N_PROPERTIES,
				sizeof(*pseudo->pending));
		if (pseudo->pending == NULL)
			return CSS_NOMEM;
	}

	error = var_source_for_sheet(style->sheet, &source);
	if (error != CSS_OK)
		return error;

	if (css__outranks_existing(op, isImportant(opv), state,
			FLAG_VALUE_VAR) == false)
		return CSS_OK;

	pending = &pseudo->pending[op];
	lwc_string_unref(pending->name);
	lwc_string_unref(pending->value);
	if (pending->source != NULL)
		css__var_source_unref(pending->source);

	pending->op = op;
	pending->name = lwc_string_ref(name);
	pending->value = lwc_string_ref(value);
	pending->source = var_source_ref(source);

	return CSS_OK;
}

/**
 * Make the partial style's custom properties from the selection data
 */
static css_error select_vars_make(css_select_state *state,
		enum css_pseudo_element pseudo_element,
		css_computed_vars **result)
{
	struct select_vars_pseudo *pseudo =
			&state->vars->pseudo[pseudo_element];
	css_computed_vars *vars;
	uint32_t n_pending = 0;
	uint32_t i;

	*result = NULL;

	if (pseudo->pending != NULL) {
		for (i = 0; i < CSS_N_PROPERTIES; i++) {
			if (state->props[i][pseudo_element].explicit_default ==
					FLAG_VALUE_VAR)
				n_pending++;
		}
	}

	if (pseudo->n_custom == 0 && n_pending == 0)
		return CSS_OK;

	vars = calloc(1, sizeof(*vars));
	if (vars == NULL)
		return CSS_NOMEM;
	vars->refcount = 1;

	if (pseudo->n_custom > 0) {
		vars->custom = malloc(pseudo->n_custom *
				sizeof(*vars->custom));
		if (vars->custom == NULL) {
			free(vars);
			return CSS_NOMEM;
		}
		for (i = 0; i < pseudo->n_custom; i++) {
			vars->custom[i].name =
					lwc_string_ref(pseudo->custom[i].name);
			vars->custom[i].value =
					lwc_string_ref(pseudo->custom[i].value);
		}
		vars->n_custom = pseudo->n_custom;
		qsort(vars->custom, vars->n_custom, sizeof(*vars->custom),
				custom_compare);
	}

	if (n_pending > 0) {
		vars->pending = malloc(n_pending * sizeof(*vars->pending));
		if (vars->pending == NULL) {
			css__computed_vars_unref(vars);
			return CSS_NOMEM;
		}
		for (i = 0; i < CSS_N_PROPERTIES; i++) {
			css_var_pending *p = &pseudo->pending[i];

			if (state->props[i][pseudo_element].explicit_default !=
					FLAG_VALUE_VAR)
				continue;

			vars->pending[vars->n_pending].op = p->op;
			vars->pending[vars->n_pending].name =
					lwc_string_ref(p->name);
			vars->pending[vars->n_pending].value =
					lwc_string_ref(p->value);
			vars->pending[vars->n_pending].source =
					var_source_ref(p->source);
			vars->n_pending++;
		}
	}

	*result = vars;
	return CSS_OK;
}

/* Internally exported function, documented in select/vars.h */
css_error css__select_vars_finish(css_select_state *state, bool root)
{
	uint32_t j;
	css_error error;

	if (state->vars == NULL)
		return CSS_OK;

	for (j = 0; j < CSS_PSEUDO_ELEMENT_COUNT; j++) {
		css_computed_style *style = state->results->styles[j];
		css_computed_vars *own = NULL;
		css_computed_vars *resolved = NULL;

		if (style == NULL)
			continue;

		error = select_vars_make(state, j, &own);
		if (error != CSS_OK)
			return error;
		if (own == NULL)
			continue;

		if (root == false || j != CSS_PSEUDO_ELEMENT_NONE) {
			/* Resolved when composed */
			style->vars = own;
			continue;
		}

		/* The root element is not composed, so resolve it now */
		error = resolve_custom(NULL, own->custom, own->n_custom,
				&resolved);
		if (error == CSS_OK && own->n_pending > 0) {
			error = apply_all_pending(style, resolved,
					own->pending, own->n_pending, true,
					state->handler, state->pw);
		}
		css__computed_vars_unref(own);
		if (error != CSS_OK) {
			css__computed_vars_unref(resolved);
			return error;
		}

		style->vars = resolved;
	}

	return CSS_OK;
}

/* Internally exported function, documented in select/vars.h */
void css__select_vars_destroy(css_select_state *state)
{
	uint32_t i, j;

	if (state->vars == NULL)
		return;

	for (j = 0; j < CSS_PSEUDO_ELEMENT_COUNT; j++) {
		struct select_vars_pseudo *pseudo = &state->vars->pseudo[j];

		for (i = 0; i < pseudo->n_custom; i++) {
			lwc_string_unref(pseudo->custom[i].name);
			lwc_string_unref(pseudo->custom[i].value);
		}
		free(pseudo->custom);

		if (pseudo->pending != NULL) {
			for (i = 0; i < CSS_N_PROPERTIES; i++) {
				lwc_string_unref(pseudo->pending[i].name);
				lwc_string_unref(pseudo->pending[i].value);
				if (pseudo->pending[i].source != NULL)
					css__var_source_unref(
						pseudo->pending[i].source);
			}
			free(pseudo->pending);
		}
	}

	free(state->vars);
	state->vars = NULL;
}


/******************************************************************************
 * Composition                                                                *
 ******************************************************************************/

/* Internally exported function, documented in select/vars.h */
css_error css__computed_vars_compose(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style **resolved, css_computed_vars **vars)
{
	const css_computed_vars *own = child->vars;
	css_computed_vars *result = NULL;
	css_computed_style *clone = NULL;
	css_error error;

	*resolved = NULL;
	*vars = NULL;

	if (own == NULL) {
		/* Inherit the parent's custom properties */
		*vars = computed_vars_ref(parent->vars);
		return CSS_OK;
	}

	if (own->n_custom == 0) {
		/* Nothing of its own, so the parent's are the same */
		result = computed_vars_ref(parent->vars);
	} else {
		error = resolve_custom(parent->vars, own->custom,
				own->n_custom, &result);
		if (error != CSS_OK)
			return error;
	}

	if (own->n_pending > 0) {
		error = css__computed_style_clone(child, &clone);
		if (error == CSS_OK) {
			error = apply_all_pending(clone, result,
					own->pending, own->n_pending, false,
					NULL, NULL);
		}
		if (error != CSS_OK) {
			if (clone != NULL)
				css_computed_style_destroy(clone);
			css__computed_vars_unref(result);
			return error;
		}
	}

	*resolved = clone;
	*vars = result;
	return CSS_OK;
}
