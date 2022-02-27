
#include <stdarg.h>
#include "../include/css/library.h"

/**
 * @see https://developer.mozilla.org/en-US/docs/Web/CSS/Value_definition_syntax
 * @see https://drafts.csswg.org/css-values/#value-defs
 * @see https://developer.mozilla.org/en-US/docs/Web/API/CSS/RegisterProperty
 **/

#define CSS_VALDEF_PARSER_ERROR_SIZE 256

typedef enum css_valdef_sign_t {
	CSS_VALDEF_SIGN_NONE,
	CSS_VALDEF_SIGN_ALIAS,
	CSS_VALDEF_SIGN_JUXTAPOSITION,
	CSS_VALDEF_SIGN_DOUBLE_AMPERSAND,
	CSS_VALDEF_SIGN_DOUBLE_BAR,
	CSS_VALDEF_SIGN_SINGLE_BAR,
	CSS_VALDEF_SIGN_BRACKETS,
	CSS_VALDEF_SIGN_ANGLE_BRACKET
} css_valdef_sign_t;

typedef int (*css_value_parse_func_t)(css_style_value_t *, const char *);

typedef struct css_value_type_record_t {
	char *name;
	css_value_parse_func_t parse_value;
} css_value_type_record_t;

typedef struct css_valdef_t css_valdef_t;

struct css_valdef_t {
	css_valdef_sign_t sign;
	union {
		int ident;
		css_value_type_record_t *type;
		struct {
			unsigned min_length;
			unsigned max_length;

			/** list_t<css_valdef_t> */
			list_t list;
		} children;
	};
};

typedef enum css_valdef_parser_target_t {
	CSS_VALDEF_PARSER_TARGET_NONE,
	CSS_VALDEF_PARSER_TARGET_ERROR,
	CSS_VALDEF_PARSER_TARGET_KEYWORD,
	CSS_VALDEF_PARSER_TARGET_DATA_TYPE,
	CSS_VALDEF_PARSER_TARGET_SIGN
} css_valdef_parser_target_t;

typedef struct css_valdef_parser_t {
	const char *cur;
	char *buffer;
	size_t pos;
	size_t buffer_size;
	css_valdef_parser_target_t target;
	char error[CSS_VALDEF_PARSER_ERROR_SIZE];

	css_valdef_t valdef;

	/** list_t<css_valdef_parser_target_t> */
	list_t valdef_stack;
} css_valdef_parser_t;

static struct css_value_module_t {
	/** dict_t<string, css_value_type_record_t> */
	dict_t *types;

	/** dict_t<string, string> */
	dict_t *type_alias;
} css_value;

static void css_value_types_destroy_value(void *priv, void *val)
{
	css_value_type_record_t *t = val;

	free(t->name);
	free(t);
}

static void css_value_type_alias_destroy_value(void *priv, void *val)
{
	free(val);
}

void css_init_value_definitons(void)
{
	static dict_type_t type_alias_dt;
	static dict_type_t types_dt;

	dict_init_string_key_type(&types_dt);
	types_dt.val_destructor = css_value_types_destroy_value;
	dict_init_string_copy_key_type(&type_alias_dt);
	type_alias_dt.val_destructor = css_value_type_alias_destroy_value;
	css_value.types = dict_create(&types_dt, NULL);
	css_value.type_alias = dict_create(&type_alias_dt, NULL);
}

void css_destroy_value_definitons(void)
{
	dict_destroy(css_value.types);
	dict_destroy(css_value.type_alias);
	css_value.type_alias = NULL;
}

int css_register_value_type_alias(const char *type_name, const char *alias)
{
	char *stored_alias;

	if (css_get_keyword_key(alias)) {
		return -1;
	}
	stored_alias = strdup2(alias);
	return dict_add(css_value.type_alias, stored_alias, type_name);
}

const char *css_resolve_value_type(const char *alias)
{
	return dict_fetch_value(css_value.type_alias, alias);
}

const css_value_type_record_t *css_register_value_type(
    const char *type_name, css_value_parse_func_t parse)
{
	css_value_type_record_t *t;

	t = malloc(sizeof(css_value_type_record_t));
	if (!t) {
		return NULL;
	}
	t->name = strdup2(type_name);
	t->parse_value = parse;
	if (dict_add(css_value.types, t->name, t) != 0) {
		free(t->name);
		free(t);
		return NULL;
	}
	return t;
}

const css_value_type_record_t *css_get_value_type(const char *type_name)
{
	return dict_fetch_value(css_value.types, type_name);
}

css_valdef_parser_t *css_valdef_parser_create(size_t buffer_size)
{
	css_valdef_parser_t *parser;

	parser = calloc(sizeof(css_valdef_parser_t), 1);
	parser->buffer = calloc(sizeof(char), buffer_size);
	parser->buffer_size = buffer_size;
	parser->target = CSS_VALDEF_PARSER_TARGET_NONE;
	return parser;
}

void css_valdef_parser_destroy(css_valdef_parser_t *parser)
{
	free(parser->buffer);
	free(parser);
}

INLINE void css_valdef_parser_get_char(css_valdef_parser_t *parser)
{
	parser->buffer[parser->pos++] = *(parser->cur);
}

static int css_valdef_parser_error(css_valdef_parser_t *parser, const char *fmt,
				   ...)
{
	size_t len;
	va_list args;

	va_start(args, fmt);
	len =
	    vsnprintf(parser->buffer, CSS_VALDEF_PARSER_ERROR_SIZE, fmt, args);
	va_end(args);
	parser->buffer[CSS_VALDEF_PARSER_ERROR_SIZE - 1] = 0;
	parser->target = CSS_VALDEF_PARSER_TARGET_ERROR;
	return -1;
}

static int css_valdef_parser_parse_keyword(css_valdef_parser_t *parser)
{
	switch (*parser->cur) {
	CASE_WHITE_SPACE:
		css_valdef_parser_commit(parser);
		parser->target = CSS_VALDEF_PARSER_TARGET_NONE;
		return -1;
	case '|':
	case '&':
	case '[':
	case ']':
	case '<':
		css_valdef_parser_commit(parser);
		parser->target = CSS_VALDEF_PARSER_TARGET_SIGN;
		parser->buffer[0] = 0;
		parser->pos = 0;
		parser->cur--;
		break;
	case '>':
		parser->target = CSS_VALDEF_PARSER_TARGET_ERROR;
		return -1;
	default:
		css_valdef_parser_get_char(parser);
		break;
	}
	return 0;
}

static int css_valdef_push(css_valdef_t *valdef, css_valdef_t *child)
{
	return 0;
}

static int css_valdef_copy(css_valdef_t *dest, const css_valdef_t *src)
{
	return 0;
}

static int css_valdef_parser_commit(css_valdef_parser_t *parser)
{
	list_node_t *node = list_get_last_node(&parser->valdef_stack);
	css_valdef_t *parent_valdef = node ? node->data : NULL;
	css_valdef_t *alias;

	switch (parser->valdef.sign) {
	case CSS_VALDEF_SIGN_NONE:
		alias = css_get_value_type_alias(parser->buffer);
		if (alias) {
			css_valdef_copy(&parser->valdef, alias);
			break;
		}
		parser->valdef.ident = css_get_keyword_key(parser->buffer);
		if (parser->valdef.ident == -1) {
			parser->target = CSS_VALDEF_PARSER_TARGET_ERROR;
			css_valdef_parser_error("unknown keyword: `%s`\n",
						parser->buffer);
			return -1;
		}
		break;
	case CSS_VALDEF_SIGN_ANGLE_BRACKET:
		parser->valdef.type = css_get_value_type(parser->buffer);
		if (!parser->valdef.type) {
			return css_valdef_parser_error(
			    parser, "unknown data type: %s\n", parser->buffer);
		}
		break;
	default:
		return -1;
	}
	if (parent_valdef) {
		css_valdef_append_option(parent_valdef, &parser->valdef);
	}
	return 0;
}

int css_valdef_parser_output(css_valdef_parser_t *parser, css_valdef_t *val)
{
	return 0;
}

static int css_valdef_parser_open_bracket(css_valdef_parser_t *parser)
{
	css_valdef_t *group;

	group = malloc(sizeof(css_valdef_t));
	if (!group) {
		return -1;
	}
	group->children.max_length = 1;
	group->children.min_length = 1;
	group->sign = CSS_VALDEF_SIGN_BRACKETS;
	list_create(&group->children.list);
	list_append(&parser->valdef_stack, group);
	return 0;
}

static int css_valdef_parser_close_bracket(css_valdef_parser_t *parser)
{
	list_node_t *node = list_get_last_node(&parser->valdef_stack);
	css_valdef_t *parent_valdef = node ? node->data : NULL;

	if (!parent_valdef) {
		parser->target = CSS_VALDEF_PARSER_TARGET_ERROR;
		return -1;
	}
	list_delete_last(&parser->valdef_stack);
	return 0;
}

static int css_valdef_parser_convert_valdef_sign(css_valdef_parser_t *parser,
						 css_valdef_sign_t sign)
{
	css_valdef_t *parent_valdef;
	css_valdef_t *child;

	// TODO: && 和 || 混写时有点问题

	parent_valdef = parser->valdef_stack.tail.data;
	if (parent_valdef && parent_valdef->sign == sign) {
		return 0;
	}
	child = malloc(sizeof(css_valdef_t));
	if (!child) {
		return -1;
	}
	if (!parent_valdef) {
		parent_valdef = malloc(sizeof(css_valdef_t));
		if (!child) {
			free(child);
			return -1;
		}
		*child = parser->valdef;
		list_append(&parser->valdef_stack, parent_valdef);
	} else {
		*child = *parent_valdef;
	}
	parent_valdef->sign = sign;
	list_create(&parent_valdef->children.list);
	list_append(&parent_valdef->children.list, child);
	return 0;
}

static int css_valdef_parser_parse_data_type(css_valdef_parser_t *parser)
{
	switch (*parser->cur) {
	case '<':
	case '&':
	case '?':
	case '|':
	CASE_WHITE_SPACE:
		return css_valdef_parser_error(parser, "syntax error");
	case '>':
		css_valdef_parser_commit(parser);
		parser->target = CSS_VALDEF_PARSER_TARGET_NONE;
		break;
	default:
		css_valdef_parser_get_char(parser);
		break;
	}
	return 0;
}

static int css_valdef_parser_parse_sign(css_valdef_parser_t *parser)
{
	switch (*parser->cur) {
	case '|':
	case '&':
		css_valdef_parser_get_char(parser);
		break;
	case '[':
		css_valdef_parser_commit(parser);
		css_valdef_parser_open_bracket(parser);
		return 0;
	case ']':
		css_valdef_parser_commit(parser);
		css_valdef_parser_close_bracket(parser);
		return 0;
	case '<':
		parser->valdef.sign = CSS_VALDEF_SIGN_ANGLE_BRACKET;
		parser->valdef.ident = 0;
		parser->target = CSS_VALDEF_PARSER_TARGET_DATA_TYPE;
		return 0;
	case '{':
	case '}':
		parser->target = CSS_VALDEF_PARSER_TARGET_ERROR;
		return -1;
	CASE_WHITE_SPACE:
		return 0;
	default:
		parser->target = CSS_VALDEF_PARSER_TARGET_KEYWORD;
		break;
	}
	if (parser->pos > 2) {
		parser->target = CSS_VALDEF_PARSER_TARGET_ERROR;
		return -1;
	}
	if (parser->buffer[0] == '|') {
		if (parser->buffer[1] == '|') {
			// TODO
			return 0;
		}
		if (parser->pos < 2) {
			css_valdef_parser_commit(parser);
			css_valdef_parser_convert_valdef_sign(
			    parser, CSS_VALDEF_SIGN_SINGLE_BAR);
		}
	} else if (parser->buffer[0] == '&') {
		if (parser->pos < 2) {
			css_valdef_parser_commit(parser);
			return 0;
		}
		if (parser->buffer[1] != '&') {
			return css_valdef_parser_error("unknown sign: `%s`\n",
						       parser->buffer);
		}
		css_valdef_parser_convert_valdef_sign(
		    parser, CSS_VALDEF_SIGN_DOUBLE_AMPERSAND);
		return 0;
	}
	if (parser->target = CSS_VALDEF_PARSER_TARGET_KEYWORD) {
		parser->buffer[0] = 0;
		parser->pos = 0;
		parser->cur--;
		return 0;
	}
	parser->target = CSS_VALDEF_PARSER_TARGET_ERROR;
	return -1;
}

static int css_valdef_parser_parse_target(css_valdef_parser_t *parser)
{
	switch (*parser->cur) {
	case '|':
	case '&':
	case '<':
		parser->target = CSS_VALDEF_PARSER_TARGET_SIGN;
		parser->buffer[0] = 0;
		parser->pos = 0;
		parser->cur--;
		break;
	case '>':
	case ']':
		parser->target = CSS_VALDEF_PARSER_TARGET_ERROR;
		return -1;
	CASE_WHITE_SPACE:
		return -1;
	default:
		parser->target = CSS_VALDEF_PARSER_TARGET_KEYWORD;
		parser->buffer[0] = 0;
		parser->pos = 0;
		parser->cur--;
		break;
	}
	return 0;
}

static size_t css_valdef_parser_parse(css_valdef_parser_t *parser,
				      const char *str)
{
	size_t size = 0;

	parser->cur = str;
	while (*parser->cur && size < parser->buffer_size) {
		switch (parser->target) {
		case CSS_VALDEF_PARSER_TARGET_NONE:
			css_valdef_parser_parse_target(parser);
			break;
		case CSS_VALDEF_PARSER_TARGET_ERROR:
			return 0;
		default:
			break;
		}
		++parser->cur;
		++size;
	}
	return size;
}

int css_compile_valdef(const char *str, css_valdef_t *out_val)
{
	size_t len;
	const char *p;
	css_valdef_parser_t *parser;

	parser = css_valdef_parser_create(512);
	for (len = 1, p = str; len > 0; p += len) {
		len = css_valdef_parser_parse(parser, p);
		if (parser->target == CSS_VALDEF_PARSER_TARGET_ERROR) {
			css_valdef_parser_destroy(parser);
			return -1;
		}
	}
	css_valdef_parser_output(parser, out_val);
	css_valdef_parser_destroy(parser);
	return 0;
}

int css_parse_value(const css_valdef_t *valdef, const char *str,
		    css_style_value_t *val)
{
	return 0;
}
