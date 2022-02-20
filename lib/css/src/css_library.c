/*
 * css_library.c -- CSS library operation module.
 *
 * Copyright (c) 2018, Liu chao <lc-soft@live.cn> All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of LCUI nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/css/library.h"

#define MAX_NAME_LEN 256
#define LEN(A) sizeof(A) / sizeof(*A)

enum css_selector_rank {
	GENERAL_RANK = 0,
	TYPE_RANK = 1,
	CLASS_RANK = 10,
	PCLASS_RANK = 10,
	ID_RANK = 100
};

enum css_selector_name_finder_level {
	LEVEL_NONE,
	LEVEL_TYPE,
	LEVEL_ID,
	LEVEL_CLASS,
	LEVEL_CLASS_2,
	LEVEL_STATUS,
	LEVEL_STATUS_2,
	LEVEL_TOTAL_NUM
};

/** dict_t<string, css_style_link_group_t*> */
typedef dict_t css_style_group_t;

/* 样式表查找器的上下文数据结构 */
typedef struct css_selector_name_collector_t {
	int level;    /**< 当前选择器层级 */
	int class_i;  /**< 当前处理到第几个类名 */
	int status_i; /**< 当前处理到第几个状态名（伪类名） */
	int name_i;   /**< 选择器名称从第几个字符开始 */
	char name[MAX_NAME_LEN];   /**< 选择器名称缓存 */
	css_selector_node_t *node; /**< 针对的选择器结点 */
} css_selector_name_collector_t;

/** 样式链接记录组 */
typedef struct css_style_link_group_t {
	dict_t *links;              /**< 样式链接表 */
	char *name;                 /**< 选择器名称 */
	css_selector_node_t *snode; /**< 选择器结点 */
} css_style_link_group_t;

/** 样式结点记录 */
typedef struct css_style_rule_t {
	int rank;                /**< 权值，决定优先级 */
	int batch_num;           /**< 批次号 */
	char *space;             /**< 所属的空间 */
	char *selector;          /**< 选择器 */
	css_style_props_t *list; /**< 样式表 */
	list_node_t node;        /**< 在链表中的结点 */
} css_style_rule_t;

/** 样式链接记录 */
typedef struct css_style_link_t {
	char *selector;                /**< 选择器 */
	css_style_link_group_t *group; /**< 所属组 */
	list_t styles;                 /**< 作用于当前选择器的样式 */
	dict_t *parents;               /**< 父级节点 */
} css_style_link_t;

static struct css_module_t {
	/**
	 * 样式组列表
	 * list_t<css_style_group_t*>
	 */
	list_t groups;

	/**
	 * 样式表缓存，以选择器的 hash 值索引
	 * dict_t<css_selector_hash_t, css_style_props_t*>
	 */
	dict_t *cache;

	/**
	 * 样式属性列表
	 * css_property_definition_t*[]
	 */
	css_property_definition_t **properties;
	size_t properties_length;

	/**
	 * 样式属性名称映射表
	 * dict_t<string, css_property_definition_t>
	 */
	dict_t *property_map;

	/**
	 * 样式属性值表，以值的名称索引
	 * dict_t<int, css_keyword_t*>
	 */
	dict_t *keywords;

	/**
	 * 样式属性值名称表，以值索引
	 * dict_t<string, css_keyword_t*>
	 */
	dict_t *keyword_names;

	/** 字符串池 */
	strpool_t *strpool;

	/** 当前记录的属性数量 */
	int count;
} css;

/** 样式字符串值与标识码 */
typedef struct css_keyword_t {
	int key;
	char *name;
} css_keyword_t;

/* clang-format off */

// https://developer.mozilla.org/en-US/docs/Web/API/CSS/RegisterProperty

int css_compile_syntax(const char *syntax_str, css_syntax_t *out)
{
	char name[32];
	int name_len;
	unsigned i;

	const char *p;
	struct name_value_map {
		const char *name;
		css_style_value_parsing_func_t parser;
	} map[] = {
		{ "length", css_parse_length_value },
		{ "percentage", css_parse_percentage_value },
		{ "keyword", css_parse_keyword_value },
		{ "color", css_parse_color_value },
		{ "image", css_parse_image_value },
	};
	const unsigned map_size = sizeof(map) / sizeof(struct name_value_map);

	out->length = 0;
	for (p = syntax_str; *p; ++p) {
		if (*p == '<') {
			name_len = 0;
			continue;
		}
		if (p == '|') {
			return p + 1;
		}
		if (*p != '>') {
			if (name_len == -1) {
				continue;
			}
			if (name_len > sizeof(name)) {
				return NULL;
			}
			name[name_len++] = *p;
			continue;
		}
		name[name_len] = 0;
		name_len = -1;
		for (i = 0; i < map_size; ++i) {
			if (strcmp(name, map[i].name) == 0) {
				out->parsers[out->length++] = map[i].parser;
				break;
			}
		}
		if (i == map_size) {
			return NULL;
		}
	}
	return 0;
}

int css_parse_style_value_with_syntax(css_syntax_t *syntax, const char *str,
				      css_style_value_t *val)
{
	unsigned i;

	for (i = 0; i < syntax->length; ++i) {
		if (syntax->parsers[i](str, val) == 0) {
			return 0;
		}
	}
	return -1;
}

static void css_property_definition_destroy(css_property_definition_t *prop)
{
	if (prop->name) {
		free(prop->name);
		prop->name = NULL;
	}
	free(prop);
}

static int css_register_property_with_key(int key, const char *name, const char *syntax,
			  const char *initial_value)
{
	css_property_definition_t *prop;
	css_property_definition_t **props;

	if (key >= css.properties_length) {
		props = realloc(css.properties, key * sizeof(css_property_definition_t *));
		if (!props) {
			return -1;
		}
		css.properties = props;
		css.properties_length = key;
	}
	prop = malloc(sizeof(css_property_definition_t));
	if (prop) {
		return -1;
	}
	if (css_compile_syntax(syntax, &prop->syntax) != 0) {
		css_property_definition_destroy(prop);
		return -2;
	}
	css_parse_style_value_with_syntax(&prop->syntax, initial_value,
					  &prop->initial_value);
	prop->name = strdup2(name);
	prop->key = key;
	props[prop->key] = prop;
	dict_add(css.property_map, prop->name, prop);
	css.count++;
	return prop->key;
}

int css_register_property(const char *name, const char *syntax,
			  const char *initial_value)
{
	return css_register_property_with_key((int)css.properties_length, name, syntax, initial_value);
}

const css_property_definition_t *css_get_property(const char *name)
{
	return dict_fetch_value(css.properties, name);
}

const css_property_definition_t *css_get_property_by_key(int key)
{
	if (key >= 0 && (size_t)key < css.properties_length) {
		return css.properties[key];
	}
	return NULL;
}

static css_keyword_t *keyword_create(int key, const char *name)
{
	css_keyword_t *kw;
	kw = malloc(sizeof(css_keyword_t));
	kw->name = strdup2(name);
	kw->key = key;
	return kw;
}

static void keyword_destroy(void *data)
{
	css_keyword_t *kw = data;
	free(kw->name);
	free(kw);
}

static void keyword_destructor(void *privdata, void *data)
{
	keyword_destroy(data);
}

int css_register_keyword(int key, const char *name)
{
	css_keyword_t *kw = keyword_create(key, name);
	if (dict_add(css.keywords, kw->name, kw)) {
		keyword_destroy(kw);
		return -1;
	}
	if (dict_add(css.keyword_names, &kw->key, kw)) {
		keyword_destroy(kw);
		return -2;
	}
	return 0;
}

int css_get_keyword_key(const char *str)
{
	css_keyword_t *kw;
	kw = dict_fetch_value(css.keywords, str);
	if (kw) {
		return kw->key;
	}
	return -1;
}

const char *css_get_keyword_name(int val)
{
	css_keyword_t *kw;
	kw = dict_fetch_value(css.keyword_names, &val);
	if (kw) {
		return kw->name;
	}
	return NULL;
}

int css_get_property_count(void)
{
	return css.count;
}

LCUI_BOOL css_selector_node_match(css_selector_node_t *sn1, css_selector_node_t *sn2)
{
	int i, j;
	if (sn2->id) {
		if (!sn1->id || strcmp(sn1->id, sn2->id) != 0) {
			return FALSE;
		}
	}
	if (sn2->type && strcmp(sn2->type, "*") != 0) {
		if (!sn1->type || strcmp(sn1->type, sn2->type) != 0) {
			return FALSE;
		}
	}
	if (sn2->classes) {
		if (!sn1->classes) {
			return FALSE;
		}
		for (i = 0; sn2->classes[i]; ++i) {
			for (j = 0; sn1->classes[j]; ++j) {
				if (strcmp(sn2->classes[i], sn1->classes[i]) ==
				    0) {
					j = -1;
					break;
				}
			}
			if (j != -1) {
				return FALSE;
			}
		}
	}
	if (sn2->status) {
		if (!sn1->status) {
			return FALSE;
		}
		for (i = 0; sn2->status[i]; ++i) {
			for (j = 0; sn1->status[j]; ++j) {
				if (strcmp(sn2->status[i], sn1->status[i]) ==
				    0) {
					j = -1;
					break;
				}
			}
			if (j != -1) {
				return FALSE;
			}
		}
	}
	return TRUE;
}

static void css_selector_node_copy(css_selector_node_t *dst, css_selector_node_t *src)
{
	int i;
	dst->id = src->id ? strdup2(src->id) : NULL;
	dst->type = src->type ? strdup2(src->type) : NULL;
	dst->fullname = src->fullname ? strdup2(src->fullname) : NULL;
	if (src->classes) {
		for (i = 0; src->classes[i]; ++i) {
			strlist_sorted_add(&dst->classes, src->classes[i]);
		}
	}
	if (src->status) {
		for (i = 0; src->status[i]; ++i) {
			strlist_sorted_add(&dst->status, src->status[i]);
		}
	}
}

void css_selector_node_destroy(css_selector_node_t *node)
{
	if (node->type) {
		free(node->type);
		node->type = NULL;
	}
	if (node->id) {
		free(node->id);
		node->id = NULL;
	}
	if (node->classes) {
		strlist_free(node->classes);
		node->classes = NULL;
	}
	if (node->status) {
		strlist_free(node->status);
		node->status = NULL;
	}
	if (node->fullname) {
		free(node->fullname);
		node->fullname = NULL;
	}
	free(node);
}

void css_selector_destroy(css_selector_t *s)
{
	int i;
	for (i = 0; i < CSS_SELECTOR_MAX_DEPTH; ++i) {
		if (!s->nodes[i]) {
			break;
		}
		css_selector_node_destroy(s->nodes[i]);
		s->nodes[i] = NULL;
	}
	s->rank = 0;
	s->length = 0;
	s->batch_num = 0;
	free(s->nodes);
	free(s);
}

css_style_props_t *css_style_properties_create(void)
{
	css_style_props_t *list;

	list = malloc(sizeof(css_style_props_t));
	list_create(list);
	return list;
}

void css_unit_value_destroy(css_unit_value_t *s)
{
	switch (s->unit) {
	case CSS_UNIT_STRING:
		if (s->is_valid && s->string) {
			free(s->string);
		}
		s->string = NULL;
		break;
	case CSS_UNIT_WSTRING:
		if (s->is_valid && s->string) {
			free(s->wstring);
		}
		s->wstring = NULL;
		break;
	default:
		break;
	}
	s->is_valid = FALSE;
}

void css_unit_value_merge(css_unit_value_t *dst, css_unit_value_t *src)
{
	switch (src->unit) {
	case CSS_UNIT_STRING:
		dst->string = strdup2(src->string);
		break;
	case CSS_UNIT_WSTRING:
		dst->wstring = wcsdup2(src->wstring);
		break;
	default:
		*dst = *src;
		break;
	}
	dst->is_valid = TRUE;
	dst->unit = src->unit;
}

static void css_style_property_destroy(css_style_property_t *node)
{
	css_unit_value_destroy(&node->style);
	free(node);
}

void css_style_properties_destroy(css_style_props_t *list)
{
	list_destroy_without_node(list, (list_item_destructor_t)css_style_property_destroy);
	free(list);
}

css_style_decl_t *css_style_declaration_create(void)
{
	css_style_decl_t *ss;

	ss = calloc(sizeof(css_style_decl_t), 1);
	if (!ss) {
		return ss;
	}
	ss->length = css_get_property_count();
	ss->sheet = calloc(sizeof(css_unit_value_t), ss->length + 1);
	return ss;
}

void css_style_declaration_clear(css_style_decl_t *ss)
{
	int i;

	for (i = 0; i < ss->length; ++i) {
		css_unit_value_destroy(&ss->sheet[i]);
	}
}

void css_style_declaration_destroy(css_style_decl_t *ss)
{
	css_style_declaration_clear(ss);
	free(ss->sheet);
	free(ss);
}

css_style_property_t *css_style_properties_find(css_style_props_t *list, int key)
{
	list_node_t *node;
	css_style_property_t *snode;

	for (list_each(node, list)) {
		snode = node->data;
		if (snode->key == key) {
			return snode;
		}
	}
	return NULL;
}

int css_style_properties_remove(css_style_props_t *list, int key)
{
	list_node_t *node;
	css_style_property_t *snode;

	for (list_each(node, list)) {
		snode = node->data;
		if (snode->key == key) {
			list_unlink(list, node);
			css_unit_value_destroy(&snode->style);
			free(snode);
			return 0;
		}
	}
	return -1;
}

css_style_property_t *css_style_properties_add(css_style_props_t *list, int key)
{
	css_style_property_t *node;

	node = malloc(sizeof(css_style_property_t));
	node->key = key;
	node->style.is_valid = FALSE;
	node->style.unit = CSS_UNIT_NONE;
	node->node.data = node;
	list_append_node(list, &node->node);
	return node;
}

static unsigned css_style_properties_merge(css_style_props_t *list, const css_style_decl_t *sheet)
{
	int i, count;
	css_style_property_t *node;

	for (count = 0, i = 0; i < sheet->length; ++i) {
		if (!sheet->sheet[i].is_valid) {
			continue;
		}
		node = css_style_properties_add(list, i);
		css_unit_value_merge(&node->style, &sheet->sheet[i]);
		count += 1;
	}
	return count;
}

int css_style_declaration_merge(css_style_decl_t *dest, const css_style_decl_t *src)
{
	size_t i;
	size_t size;
	css_unit_value_t *s;

	if (src->length > dest->length) {
		size = sizeof(css_unit_value_t) * src->length;
		s = realloc(dest->sheet, size);
		if (!s) {
			return -1;
		}
		for (i = dest->length; i < src->length; ++i) {
			s[i].is_valid = FALSE;
		}
		dest->sheet = s;
		dest->length = src->length;
	}
	for (i = 0; i < src->length; ++i) {
		if (src->sheet[i].is_valid && !dest->sheet[i].is_valid) {
			css_unit_value_merge(&dest->sheet[i], &src->sheet[i]);
		}
	}
	return 0;
}

int css_style_declaration_merge_properties(css_style_decl_t *ss, css_style_props_t *list)
{
	css_unit_value_t *s;
	css_style_property_t *snode;
	list_node_t *node;
	size_t size;
	size_t i = 0, count = 0;

	for (list_each(node, list)) {
		snode = node->data;
		if (snode->key > ss->length) {
			size = sizeof(css_unit_value_t) * (snode->key + 1);
			s = realloc(ss->sheet, size);
			if (!s) {
				return -1;
			}
			for (i = ss->length; i <= snode->key; ++i) {
				s[i].is_valid = FALSE;
			}
			ss->sheet = s;
			ss->length = snode->key + 1;
		}
		if (!ss->sheet[snode->key].is_valid && snode->style.is_valid) {
			css_unit_value_merge(&ss->sheet[snode->key], &snode->style);
			++count;
		}
	}
	return (int)count;
}

int css_style_declaration_replace(css_style_decl_t *dest, const css_style_decl_t *src)
{
	size_t i;
	css_unit_value_t *s;
	size_t count, size;

	if (src->length > dest->length) {
		size = sizeof(css_unit_value_t) * src->length;
		s = realloc(dest->sheet, size);
		if (!s) {
			return -1;
		}
		for (i = dest->length; i < src->length; ++i) {
			s[i].is_valid = FALSE;
		}
		dest->sheet = s;
		dest->length = src->length;
	}
	for (count = 0, i = 0; i < src->length; ++i) {
		if (!src->sheet[i].is_valid) {
			continue;
		}
		css_unit_value_destroy(&dest->sheet[i]);
		css_unit_value_merge(&dest->sheet[i], &src->sheet[i]);
		++count;
	}
	return (int)count;
}

static void css_selector_init_name_collector(css_selector_name_collector_t *sfinder, css_selector_node_t *snode)
{
	sfinder->level = 0;
	sfinder->class_i = 0;
	sfinder->name_i = 0;
	sfinder->status_i = 0;
	sfinder->name[0] = 0;
	sfinder->node = snode;
}

static void css_selector_destroy_name_collector(css_selector_name_collector_t *sfinder)
{
	sfinder->name_i = 0;
	sfinder->name[0] = 0;
	sfinder->class_i = 0;
	sfinder->status_i = 0;
	sfinder->node = NULL;
	sfinder->level = LEVEL_NONE;
}

/* 生成选择器全名列表 */
static int css_selector_collect_name(css_selector_name_collector_t *sfinder, list_t *list)
{
	size_t len, old_len;
	int i, old_level, count = 0;
	char *fullname = sfinder->name + sfinder->name_i;

	old_len = len = strlen(fullname);
	old_level = sfinder->level;
	switch (sfinder->level) {
	case LEVEL_TYPE:
		/* 按类型选择器生成选择器全名 */
		if (!sfinder->node->type) {
			return 0;
		}
		strcpy(fullname, sfinder->node->type);
		list_append(list, strdup2(fullname));
		break;
	case LEVEL_ID:
		/* 按ID选择器生成选择器全名 */
		if (!sfinder->node->id) {
			return 0;
		}
		fullname[len++] = '#';
		fullname[len] = 0;
		strcpy(fullname + len, sfinder->node->id);
		list_append(list, strdup2(fullname));
		break;
	case LEVEL_CLASS:
		if (!sfinder->node->classes) {
			return 0;
		}
		/* 按类选择器生成选择器全名
		 * 假设当前选择器全名为：textview#main-btn-text，且有 .a .b .c
		 * 这三个类，那么下面的处理将会拆分成以下三个选择器：
		 * textview#test-text.a
		 * textview#test-text.b
		 * textview#test-text.a
		 */
		fullname[len++] = '.';
		for (i = 0; sfinder->node->classes[i]; ++i) {
			sfinder->level += 1;
			sfinder->class_i = i;
			strcpy(fullname + len, sfinder->node->classes[i]);
			list_append(list, strdup2(fullname));
			/* 将当前选择器名与其它层级的选择器名组合 */
			while (sfinder->level < LEVEL_TOTAL_NUM) {
				count += css_selector_collect_name(sfinder, list);
				sfinder->level += 1;
			}
			sfinder->level = LEVEL_CLASS;
		}
		sfinder->level = LEVEL_CLASS;
		fullname[old_len] = 0;
		sfinder->class_i = 0;
		return count;
	case LEVEL_CLASS_2:
		if (!sfinder->node->classes) {
			return 0;
		}
		/* 按类选择器生成选择器全名，结果类似于这样：
		 * textview#test-text.a.b
		 * textview#test-text.a.c
		 * textview#test-text.b.c
		 * textview#test-text.a.b.c
		 */
		fullname[len++] = '.';
		for (i = 0; sfinder->node->classes[i]; ++i) {
			if (i <= sfinder->class_i) {
				continue;
			}
			strcpy(fullname + len, sfinder->node->classes[i]);
			list_append(list, strdup2(fullname));
			sfinder->class_i = i;
			count += css_selector_collect_name(sfinder, list);
			sfinder->class_i = 0;
			sfinder->level = LEVEL_STATUS;
			/**
			 * 递归拼接伪类名，例如：
			 * textview#main-btn-text:active
			 */
			count += css_selector_collect_name(sfinder, list);
			sfinder->level = LEVEL_CLASS_2;
		}
		fullname[old_len] = 0;
		sfinder->level = LEVEL_CLASS_2;
		return count;
	case LEVEL_STATUS:
		if (!sfinder->node->status) {
			return 0;
		}
		fullname[len++] = ':';
		sfinder->level = LEVEL_STATUS_2;
		/**
		 * 按伪类选择器生成选择器全名
		 * 假设当前选择器全名为：textview#main-btn-text:hover:focus:active
		 * 那么下面的循环会将它拆分为以下几个选择器：
		 * textview#main-btn-text:active
		 * textview#main-btn-text:active:focus
		 * textview#main-btn-text:active:focus:hover
		 * textview#main-btn-text:active:hover
		 * textview#main-btn-text:focus
		 * textview#main-btn-text:focus:hover
		 * textview#main-btn-text:hover
		 */
		for (i = 0; sfinder->node->status[i]; ++i) {
			sfinder->status_i = i;
			strcpy(fullname + len, sfinder->node->status[i]);
			list_append(list, strdup2(fullname));
			/**
			 * 递归调用，以一层层拼接出像下面这样的选择器：
			 * textview#main-btn-text:active:focus:hover
			 */
			count += css_selector_collect_name(sfinder, list);
		}
		sfinder->level = LEVEL_STATUS;
		fullname[old_len] = 0;
		sfinder->status_i = 0;
		return count;
	case LEVEL_STATUS_2:
		if (!sfinder->node->status) {
			return 0;
		}
		/** 按伪类选择器生成选择器全名 */
		for (i = 0; sfinder->node->status[i]; ++i) {
			if (i <= sfinder->status_i) {
				continue;
			}
			fullname[len] = ':';
			strcpy(fullname + len + 1, sfinder->node->status[i]);
			list_append(list, strdup2(fullname));
			sfinder->status_i = i;
			count += css_selector_collect_name(sfinder, list);
			sfinder->status_i = 0;
		}
		fullname[old_len] = 0;
		return count;
	default:
		break;
	}
	for (i = sfinder->level + 1; i < LEVEL_TOTAL_NUM; ++i) {
		if (i == LEVEL_STATUS_2 || i == LEVEL_CLASS_2) {
			continue;
		}
		sfinder->level = i;
		count += css_selector_collect_name(sfinder, list);
	}
	fullname[old_len] = 0;
	sfinder->level = old_level;
	return count;
}

static int SelectorNode_Save(css_selector_node_t *node, const char *name, int len,
			     char type)
{
	char *str;
	if (len < 1) {
		return 0;
	}
	switch (type) {
	case 0:
		if (node->type) {
			break;
		}
		len += 1;
		str = malloc(sizeof(char) * len);
		strncpy(str, name, len);
		node->type = str;
		return TYPE_RANK;
	case ':':
		if (strlist_sorted_add(&node->status, name) == 0) {
			return PCLASS_RANK;
		}
		break;
	case '.':
		if (strlist_sorted_add(&node->classes, name) == 0) {
			return CLASS_RANK;
		}
		break;
	case '#':
		if (node->id) {
			break;
		}
		len += 1;
		str = malloc(sizeof(char) * len);
		strncpy(str, name, len);
		node->id = str;
		return ID_RANK;
	default:
		break;
	}
	return 0;
}

int css_selector_node_get_name_list(css_selector_node_t *sn, list_t *names)
{
	int count;
	css_selector_name_collector_t sfinder;
	css_selector_init_name_collector(&sfinder, sn);
	count = css_selector_collect_name(&sfinder, names);
	css_selector_destroy_name_collector(&sfinder);
	return count;
}

int css_selector_node_update(css_selector_node_t *node)
{
	size_t i, len = 0;
	char *fullname;

	node->rank = 0;
	if (node->id) {
		len += strlen(node->id) + 1;
		node->rank += ID_RANK;
	}
	if (node->type) {
		len += strlen(node->type) + 1;
		node->rank += TYPE_RANK;
	}
	if (node->classes) {
		for (i = 0; node->classes[i]; ++i) {
			len += strlen(node->classes[i]) + 1;
			node->rank += CLASS_RANK;
		}
	}
	if (node->status) {
		for (i = 0; node->status[i]; ++i) {
			len += strlen(node->status[i]) + 1;
			node->rank += PCLASS_RANK;
		}
	}
	if (len > 0) {
		fullname = malloc(sizeof(char) * (len + 1));
		if (!fullname) {
			return -ENOMEM;
		}
		fullname[0] = 0;
		if (node->type) {
			strcat(fullname, node->type);
		}
		if (node->id) {
			strcat(fullname, "#");
			strcat(fullname, node->id);
		}
		if (node->classes) {
			for (i = 0; node->classes[i]; ++i) {
				strcat(fullname, ".");
				strcat(fullname, node->classes[i]);
			}
			len += 1;
		}
		if (node->status) {
			for (i = 0; node->status[i]; ++i) {
				strcat(fullname, ":");
				strcat(fullname, node->status[i]);
			}
			len += 1;
		}
	} else {
		fullname = NULL;
	}
	if (node->fullname) {
		free(node->fullname);
	}
	node->fullname = fullname;
	return 0;
}

void css_selector_update(css_selector_t *s)
{
	int i;
	const unsigned char *p;
	unsigned int hash = 5381;
	for (i = 0; i < s->length; ++i) {
		p = (unsigned char *)s->nodes[i]->fullname;
		while (*p) {
			hash = ((hash << 5) + hash) + (*p++);
		}
	}
	s->hash = hash;
}

int css_selector_append(css_selector_t *selector, css_selector_node_t *node)
{
	const unsigned char *p;

	if (selector->length >= CSS_SELECTOR_MAX_DEPTH) {
		logger_warning("[css] warning: the number of nodes in the "
			       "selector has exceeded the %d limit\n",
			       CSS_SELECTOR_MAX_DEPTH);
		return -1;
	}
	selector->nodes[selector->length++] = node;
	selector->nodes[selector->length] = NULL;
	p = (unsigned char *)node->fullname;
	while (*p) {
		selector->hash = ((selector->hash << 5) + selector->hash) + (*p++);
	}
	return 0;
}

css_selector_t *css_selector_create(const char *selector)
{
	const char *p;
	int ni, si, rank;
	static int batch_num = 0;
	char type = 0, name[MAX_NAME_LEN];
	LCUI_BOOL is_saving = FALSE;
	css_selector_node_t *node = NULL;
	css_selector_t *s = calloc(sizeof(css_selector_t), 1);

	s->batch_num = ++batch_num;
	s->nodes = calloc(sizeof(css_selector_node_t), CSS_SELECTOR_MAX_DEPTH);
	if (!selector) {
		s->length = 0;
		s->nodes[0] = NULL;
		return s;
	}
	for (ni = 0, si = 0, p = selector; *p; ++p) {
		if (!node && is_saving) {
			node = calloc(sizeof(css_selector_node_t), 1);
			if (si >= CSS_SELECTOR_MAX_DEPTH) {
				logger_warning(
				    "%s: selector node list is too long.\n",
				    selector);
				return NULL;
			}
			s->nodes[si] = node;
		}
		switch (*p) {
		case ':':
		case '.':
		case '#':
			if (!is_saving) {
				is_saving = TRUE;
				type = *p;
				continue;
			}
			/* 保存上个结点 */
			rank = SelectorNode_Save(node, name, ni, type);
			is_saving = TRUE;
			type = *p;
			if (rank > 0) {
				s->rank += rank;
				ni = 0;
				continue;
			}
			logger_error("%s: invalid selector node at %ld.\n",
				     selector, p - selector - ni);
			css_selector_node_destroy(node);
			node = NULL;
			ni = 0;
			continue;
		case ' ':
		case '\r':
		case '\n':
		case '\t':
			if (!is_saving) {
				ni = 0;
				node = NULL;
				continue;
			}
			is_saving = FALSE;
			rank = SelectorNode_Save(node, name, ni, type);
			if (rank > 0) {
				css_selector_node_update(node);
				s->rank += rank;
				node = NULL;
				ni = 0;
				si++;
				continue;
			}
			logger_error("%s: invalid selector node at %ld.\n",
				     selector, p - selector - ni);
			css_selector_node_destroy(node);
			node = NULL;
			ni = 0;
			continue;
		default:
			break;
		}
		if (*p == '-' || *p == '_' || *p == '*' ||
		    (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		    (*p >= '0' && *p <= '9')) {
			if (!is_saving) {
				type = 0;
				is_saving = TRUE;
			}
			name[ni++] = *p;
			name[ni] = 0;
			continue;
		}
		logger_warning("%s: unknown char 0x%02x at %ld.\n",
			       selector, *p, p - selector);
		return NULL;
	}
	if (is_saving) {
		if (!node) {
			node = calloc(sizeof(css_selector_node_t), 1);
			if (si >= CSS_SELECTOR_MAX_DEPTH) {
				logger_warning(
				    "%s: selector node list is too long.\n",
				    selector);
				return NULL;
			}
			s->nodes[si] = node;
		}
		rank = SelectorNode_Save(s->nodes[si], name, ni, type);
		if (rank > 0) {
			css_selector_node_update(s->nodes[si]);
			s->rank += rank;
			si++;
		} else {
			css_selector_node_destroy(s->nodes[si]);
		}
	}
	s->nodes[si] = NULL;
	s->length = si;
	css_selector_update(s);
	return s;
}

css_selector_t *css_selector_duplicate(css_selector_t *selector)
{
	int i;
	css_selector_t *s;

	s = css_selector_create(NULL);
	for (i = 0; i < selector->length; ++i) {
		s->nodes[i] = calloc(sizeof(css_selector_node_t), 1);
		css_selector_node_copy(s->nodes[i], selector->nodes[i]);
	}
	s->nodes[selector->length] = NULL;
	s->length = selector->length;
	s->hash = selector->hash;
	s->rank  = selector->rank;
	s->batch_num = selector->batch_num;
	return s;
}

static void css_style_rule_destroy(css_style_rule_t *node)
{
	if (node->space) {
		strpool_free_str(node->space);
		node->space = NULL;
	}
	if (node->selector) {
		free(node->selector);
		node->selector = NULL;
	}
	css_style_properties_destroy(node->list);
	node->list = NULL;
	free(node);
}

static css_style_link_t *css_style_link_create(void)
{
	css_style_link_t *link = calloc(sizeof(css_style_link_t), 1);
	static dict_type_t t;

	dict_init_string_copy_key_type(&t);
	link->group = NULL;
	list_create(&link->styles);
	link->parents = dict_create(&t, NULL);
	return link;
}

static void css_style_link_destroy(css_style_link_t *link)
{
	dict_destroy(link->parents);
	list_destroy_without_node(&link->styles, (list_item_destructor_t)css_style_rule_destroy);
	free(link->selector);
	link->selector = NULL;
	link->parents = NULL;
	link->group = NULL;
	free(link);
}

static void css_style_link_destructor(void *privdata, void *data)
{
	css_style_link_destroy(data);
}

static css_style_link_group_t *css_style_link_group_create(css_selector_node_t *snode)
{
	static dict_type_t dt = { 0 };
	css_style_link_group_t *group;

	dict_init_string_copy_key_type(&dt);
	dt.val_destructor = css_style_link_destructor;
	group  = calloc(sizeof(css_style_link_group_t), 1);
	group->snode = calloc(sizeof(css_selector_node_t), 1);
	group->links = dict_create(&dt, NULL);
	css_selector_node_copy(group->snode, snode);
	group->name = group->snode->fullname;
	return group;
}

static void css_style_link_group_destroy(css_style_link_group_t *group)
{
	dict_type_t *dtype;
	dtype = group->links->priv_data;
	css_selector_node_destroy(group->snode);
	dict_destroy(group->links);
	free(dtype);
	free(group);
}

static void css_style_link_group_destructor(void *privdata, void *data)
{
	css_style_link_group_destroy(data);
}

static css_style_group_t *css_style_group_create(void)
{
	static dict_type_t type = { 0 };

	dict_init_string_copy_key_type(&type);
	type.val_destructor = css_style_link_group_destructor;
	return dict_create(&type, NULL);
}

/** 根据选择器，选中匹配的样式表 */
static css_style_props_t *css_select_style_properties(css_selector_t *selector,
					   const char *space)
{
	int i, right;
	css_style_link_t *link;
	css_style_rule_t *snode;
	css_style_link_group_t *slg;
	css_selector_node_t *sn;
	css_style_group_t *group;
	dict_t *parents;

	char buf[CSS_SELECTOR_MAX_LEN];
	char fullname[CSS_SELECTOR_MAX_LEN];

	link = NULL;
	parents = NULL;
	for (i = 0, right = selector->length - 1; right >= 0; --right, ++i) {
		group = list_get(&css.groups, i);
		if (!group) {
			group = css_style_group_create();
			list_append(&css.groups, group);
		}
		sn = selector->nodes[right];
		slg = dict_fetch_value(group, sn->fullname);
		if (!slg) {
			slg = css_style_link_group_create(sn);
			dict_add(group, sn->fullname, slg);
		}
		if (i == 0) {
			strcpy(fullname, "*");
		} else {
			strcpy(fullname, buf);
		}
		link = dict_fetch_value(slg->links, fullname);
		if (!link) {
			link = css_style_link_create();
			link->group = slg;
			link->selector = strdup2(fullname);
			dict_add(slg->links, fullname, link);
		}
		if (i == 0) {
			strcpy(buf, sn->fullname);
			strcpy(fullname, buf);
		} else {
			strcpy(fullname, buf);
			sprintf(buf, "%s %s", sn->fullname, fullname);
		}
		/* 如果有上一级的父链接记录，则将当前链接添加进去 */
		if (parents) {
			if (!dict_fetch_value(parents, sn->fullname)) {
				dict_add(parents, sn->fullname, link);
			}
		}
		parents = link->parents;
	}
	if (!link) {
		return NULL;
	}
	snode = calloc(sizeof(css_style_rule_t), 1);
	if (space) {
		snode->space = strpool_alloc_str(css.strpool, space);
		strcpy(snode->space, space);
	} else {
		snode->space = NULL;
	}
	snode->node.data = snode;
	snode->list = css_style_properties_create();
	snode->rank = selector->rank;
	snode->selector = strdup2(fullname);
	snode->batch_num = selector->batch_num;
	list_append_node(&link->styles, &snode->node);
	return snode->list;
}

int css_add_style_sheet(css_selector_t *selector, css_style_decl_t *style,
		       const char *space)
{
	css_style_props_t *list;
	dict_empty(css.cache, NULL);
	list = css_select_style_properties(selector, space);
	if (list) {
		css_style_properties_merge(list, style);
	}
	return 0;
}

static size_t StyleLink_GetStyleSheets(css_style_link_t *link, list_t *outlist)
{
	size_t i;
	LCUI_BOOL found;
	css_style_rule_t *snode, *out_snode;
	list_node_t *node, *out_node;

	if (!outlist) {
		return link->styles.length;
	}
	for (list_each(node, &link->styles)) {
		i = 0;
		found = FALSE;
		snode = node->data;
		for (list_each(out_node, outlist)) {
			out_snode = out_node->data;
			if (snode->rank > out_snode->rank) {
				found = TRUE;
				break;
			}
			if (snode->rank != out_snode->rank) {
				i += 1;
				continue;
			}
			if (snode->batch_num > out_snode->batch_num) {
				found = TRUE;
				break;
			}
			i += 1;
		}
		if (found) {
			list_insert(outlist, i, snode);
		} else {
			list_append(outlist, snode);
		}
	}
	return link->styles.length;
}

static size_t css_query_selector_from_link(css_style_link_t *link, css_selector_t *s,
					  int i, list_t *list)
{
	size_t count = 0;
	css_style_link_t *parent;
	list_t names;
	list_node_t *node;
	css_selector_node_t *sn;

	list_create(&names);
	count += StyleLink_GetStyleSheets(link, list);
	while (--i >= 0) {
		sn = s->nodes[i];
		css_selector_node_get_name_list(sn, &names);
		for (list_each(node, &names)) {
			parent = dict_fetch_value(link->parents, node->data);
			if (!parent) {
				continue;
			}
			count +=
			    css_query_selector_from_link(parent, s, i, list);
		}
		list_destroy(&names, free);
	}
	return count;
}

int css_query_selector_from_group(int group, const char *name, css_selector_t *s,
				 list_t *list)
{
	int i;
	size_t count;
	dict_t *groups;
	css_style_link_group_t *slg;
	list_node_t *node;
	list_t names;

	groups = list_get(&css.groups, group);
	if (!groups || s->length < 1) {
		return 0;
	}
	count = 0;
	i = s->length - 1;
	list_create(&names);
	if (name) {
		list_append(&names, strdup2(name));
	} else {
		css_selector_node_get_name_list(s->nodes[i], &names);
		list_append(&names, strdup2("*"));
	}
	for (list_each(node, &names)) {
		dict_entry_t *entry;
		dict_iterator_t *iter;
		char *name = node->data;
		slg = dict_fetch_value(groups, name);
		if (!slg) {
			continue;
		}
		iter = dict_get_iterator(slg->links);
		while ((entry = dict_next(iter))) {
			css_style_link_t *link = dict_get_val(entry);
			count += css_query_selector_from_link(link, s, i, list);
		}
		dict_destroy_iterator(iter);
	}
	list_destroy(&names, free);
	return (int)count;
}

static void css_print_property_name(int key)
{
	const char *name;

	name = css_get_property_name(key);
	if (name) {
		logger_debug("\t%s", name);
	} else {
		logger_debug("\t<unknown style %d>", key);
	}
	logger_debug("%s: ", key > STYLE_KEY_TOTAL ? " (+)" : "");
}

static void css_style_value_print(css_style_value_t *s)
{
	switch (s->type) {
	case CSS_INVALID_VALUE:
		logger_info("<invalid value>");
		break;
	case CSS_COLOR_VALUE:
		if (s->color_value.a < 255) {
			logger_info("rgba(%d,%d,%d,%g)", s->color_value.r, s->color_value.g, s->color_value.b,
				s->color_value.a / 255.0);
		} else {
			logger_info("#%02x%02x%02x", s->color_value.r, s->color_value.g, s->color_value.b);
		}
	case CSS_IMAGE_VALUE:
		logger_info(s->image_value);
		break;
	case CSS_STRING_VALUE:
		logger_info(s->string_value);
		break;
	case CSS_KEYWORD_VALUE:
		logger_info("%s", css_get_keyword_name(s->keyword_value));
		break;
	case CSS_UNIT_VALUE:
		logger_info("%g%s", s->unit_value.value, s->unit_value.unit);
		break;
	case CSS_UNPARSED_VALUE:
		logger_info(s->unparsed_value);
	default: break;
	}
	logger_debug(";\n");
}

void css_style_properties_print(css_style_props_t *list)
{
	list_node_t *node;
	css_style_property_t *snode;

	for (list_each(node, list)) {
		snode = node->data;
		if (snode->style.type != CSS_NO_VALUE) {
			css_print_property_name(snode->key);
			css_style_value_print(&snode->style);
		}
	}
}

void css_style_declartation_print(css_style_decl_t *ss)
{
	int key;
	css_style_value_t *s;

	for (key = 0; key < ss->length; ++key) {
		s = &ss->sheet[key];
		if (s->type != CSS_NO_VALUE) {
			css_print_property_name(key);
			css_unit_value_print(s);
		}
	}
}

void css_selector_print(css_selector_t *selector)
{
	char path[CSS_SELECTOR_MAX_LEN];
	css_selector_node_t **sn;

	path[0] = 0;
	for (sn = selector->nodes; *sn; ++sn) {
		strcat(path, (*sn)->fullname);
		strcat(path, " ");
	}
	logger_debug("path: %s (rank = %d, batch_num = %d)\n", path,
		     selector->rank, selector->batch_num);
}

static void css_style_link_print(css_style_link_t *link, const char *selector)
{
	dict_entry_t *entry;
	dict_iterator_t *iter;
	list_node_t *node;
	char fullname[CSS_SELECTOR_MAX_LEN];

	if (selector) {
		sprintf(fullname, "%s %s", link->group->name, selector);
	} else {
		strcpy(fullname, link->group->name);
	}
	for (list_each(node, &link->styles)) {
		css_style_rule_t *snode = node->data;
		logger_debug("\n[%s]", snode->space ? snode->space : "<none>");
		logger_debug("[rank: %d]\n%s {\n", snode->rank, fullname);
		css_style_properties_print(snode->list);
		logger_debug("}\n");
	}
	iter = dict_get_iterator(link->parents);
	while ((entry = dict_next(iter))) {
		css_style_link_t *parent = dict_get_val(entry);
		css_style_link_print(parent, fullname);
	}
	dict_destroy_iterator(iter);
}

void css_print_all(void)
{
	dict_t *group;
	css_style_link_t *link;
	css_style_link_group_t *slg;
	dict_iterator_t *iter;
	dict_entry_t *entry;

	link = NULL;
	logger_debug("style library begin\n");
	group = list_get(&css.groups, 0);
	iter = dict_get_iterator(group);
	while ((entry = dict_next(iter))) {
		dict_entry_t *entry_slg;
		dict_iterator_t *iter_slg;

		slg = dict_get_val(entry);
		iter_slg = dict_get_iterator(slg->links);
		while ((entry_slg = dict_next(iter_slg))) {
			link = dict_get_val(entry_slg);
			css_style_link_print(link, NULL);
		}
		dict_destroy_iterator(iter_slg);
	}
	dict_destroy_iterator(iter);
	logger_debug("style library end\n");
}

const css_style_decl_t *css_get_computed_style_with_cache(css_selector_t *s)
{
	list_t list;
	list_node_t *node;
	css_style_decl_t *ss;

	list_create(&list);
	ss = dict_fetch_value(css.cache, &s->hash);
	if (ss) {
		return ss;
	}
	ss = css_style_declaration_create();
	css_query_selector(s, &list);
	for (list_each(node, &list)) {
		css_style_rule_t *sn = node->data;
		css_style_declaration_merge_properties(ss, sn->list);
	}
	list_destroy(&list, NULL);
	dict_add(css.cache, &s->hash, ss);
	return ss;
}

void css_get_computed_style(css_selector_t *s, css_style_decl_t *out_ss)
{
	const css_style_decl_t *ss;

	ss = css_get_computed_style_with_cache(s);
	css_style_declaration_clear(out_ss);
	css_style_declaration_replace(out_ss, ss);
}

void css_print_style_rules_by_selector(css_selector_t *s)
{
	list_t list;
	list_node_t *node;
	css_style_decl_t *ss;
	list_create(&list);
	ss = css_style_declaration_create();
	css_query_selector(s, &list);
	logger_debug("selector(%u) stylesheets begin\n", s->hash);
	for (list_each(node, &list)) {
		css_style_rule_t *sn = node->data;
		logger_debug("\n[%s]", sn->space ? sn->space : "<none>");
		logger_debug("[rank: %d]\n%s {\n", sn->rank, sn->selector);
		css_style_properties_print(sn->list);
		logger_debug("}\n");
		css_style_declaration_merge_properties(ss, sn->list);
	}
	list_destroy(&list, NULL);
	logger_debug("[selector(%u) final stylesheet] {\n", s->hash);
	css_style_declartation_print(ss);
	logger_debug("}\n");
	css_style_declaration_destroy(ss);
	logger_debug("selector(%u) stylesheets end\n", s->hash);
}

static void css_style_cache_destructor(void *privdata, void *val)
{
	css_style_declaration_destroy(val);
}

static void *names_dict_value_dup(void *privdata, const void *val)
{
	return strdup2(val);
}

static void names_dict_value_destructor(void *privdata, void *val)
{
	free(val);
}

static uint64_t ikey_dict_hash(const void *key)
{
	return (*(unsigned int *)key);
}

static int ikey_dict_key_compare(void *privdata, const void *key1,
				 const void *key2)
{
	return *(unsigned int *)key1 == *(unsigned int *)key2;
}

static void ikey_dict_key_destructor(void *privdata, void *key)
{
	free(key);
}

static void *ikey_dict_key_dup(void *privdata, const void *key)
{
	unsigned int *newkey = malloc(sizeof(unsigned int));
	*newkey = *(unsigned int *)key;
	return newkey;
}

static void css_init_cache(void)
{
	static dict_type_t dt = { 0 };

	dt.val_dup = NULL;
	dt.key_dup = ikey_dict_key_dup;
	dt.key_compare = ikey_dict_key_compare;
	dt.hash_function = ikey_dict_hash;
	dt.key_destructor = ikey_dict_key_destructor;
	dt.val_destructor = css_style_cache_destructor;
	dt.key_destructor = ikey_dict_key_destructor;
	css.cache = dict_create(&dt, NULL);
}

static void css_destroy_cache(void)
{
	dict_destroy(css.cache);
	css.cache = NULL;
}

static void css_init_properties(void)
{
	static dict_type_t dt = { 0 };

	dict_init_string_key_type(&dt);
	css.property_map = dict_create(&dt, NULL);
	css.properties = NULL;
	css.properties_length = 0;

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/visibility */
	css_register_property_with_key(css_key_visibility, "visibility", "visible | hidden", "visible");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_width, "width", "auto | <length> | <percentage>", "auto");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/height */
	css_register_property_with_key(css_key_height, "height", "auto | <length> | <percentage>", "auto");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/min-width */
	css_register_property_with_key(css_key_min_width, "min-width", "auto | <length> | <percentage>", "auto");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/min-height */
	css_register_property_with_key(css_key_min_height, "min-height", "auto | <length> | <percentage>", "auto");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/max-width */
	css_register_property_with_key(css_key_max_width, "max-width", "auto | <length> | <percentage>", "auto");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/max-height */
	css_register_property_with_key(css_key_max_height, "max-height", "auto | <length> | <percentage>", "auto");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/display */
	css_register_property_with_key(css_key_display, "display", "none | block | inline-block | flex", "block");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/z-index */
	css_register_property_with_key(css_key_z_index, "z-index", "auto | <integer>", "auto");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/top */
	css_register_property_with_key(css_key_top, "top", "<length> | <percentage> | auto", "auto");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSSright */
	css_register_property_with_key(css_key_right, "right", "<length> | <percentage> | auto", "auto");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/left */
	css_register_property_with_key(css_key_left, "left", "<length> | <percentage> | auto", "auto");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/bottom */
	css_register_property_with_key(css_key_bottom, "bottom", "<length> | <percentage> | auto", "auto");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/position */
	css_register_property_with_key(css_key_position, "position", "static | relative | absolute", "static");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/opacity */
	css_register_property_with_key(css_key_opacity, "opacity", "<number> | <percentage>", "1");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/vertical-align */
	css_register_property_with_key(css_key_vertical_align, "vertical-align", "middle | bottom | top", "top");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/background-color */
	css_register_property_with_key(css_key_background_color, "background-color", "<color>", "transparent");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/background-position */
	css_register_property_with_key(css_key_background_position, "background-position",
	"[\
		[ left | center | right | top | bottom | <length> | <percentage> ]\
		| [ left | center | right | <length> | <percentage> ] [ top | center | bottom | <length> | <percentage> ]
	]",
	"0% 0%");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/background-size */
	css_register_property_with_key(css_key_background_size, "background-size", "[ <length> | <percentage> | auto ]{1,2} | cover | contain", "auto auto");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/background-image */
	css_register_property_with_key(css_key_background_image, "background-image", "none | <image>", "none");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/padding-left */
	css_register_property_with_key(css_key_padding_left, "padding-left", "<length> | <percentage>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/padding-right */
	css_register_property_with_key(css_key_padding_right, "padding-right", "<length> | <percentage>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/padding-top */
	css_register_property_with_key(css_key_padding_top, "padding-top", "<length> | <percentage>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/padding-bottom */
	css_register_property_with_key(css_key_padding_bottom, "padding-bottom", "<length> | <percentage>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/margin-left */
	css_register_property_with_key(css_key_margin_left, "margin-left", "<length> | <percentage>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/margin-right */
	css_register_property_with_key(css_key_margin_right, "margin-right", "<length> | <percentage>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/margin-top */
	css_register_property_with_key(css_key_margin_top, "margin-top", "<length> | <percentage>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/margin-bottom */
	css_register_property_with_key(css_key_margin_bottom, "margin-bottom", "<length> | <percentage>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-top-color */
	css_register_property_with_key(css_key_border_top_color, "border-top-color", "<color>", "transparent");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-right-color */
	css_register_property_with_key(css_key_border_right_color, "border-right-color", "<color>", "transparent");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-bottom-color */
	css_register_property_with_key(css_key_border_bottom_color, "border-bottom-color", "<color>", "transparent");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-left-color */
	css_register_property_with_key(css_key_border_left_color, "border-left-color", "<color>", "transparent");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-top-width */
	css_register_property_with_key(css_key_border_top_width, "border-top-width", "<length>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-right-width */
	css_register_property_with_key(css_key_border_right_width, "border-right-width", "<length>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-bottom-width */
	css_register_property_with_key(css_key_border_bottom_width, "border-bottom-width", "<length>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-left-width */
	css_register_property_with_key(css_key_border_left_width, "border-left-width", "<length>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-top-width */
	css_register_property_with_key(css_key_border_top_width, "border-top-width", "<length>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-right-width */
	css_register_property_with_key(css_key_border_right_width, "border-right-width", "<length>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-bottom-width */
	css_register_property_with_key(css_key_border_bottom_width, "border-bottom-width", "<length>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-left-width */
	css_register_property_with_key(css_key_border_left_width, "border-left-width", "<length>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-top-style */
	css_register_property_with_key(css_key_border_top_style, "border-top-style", "none | solid", "none");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-right-style */
	css_register_property_with_key(css_key_border_right_style, "border-right-style", "none | solid", "none");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-bottom-style */
	css_register_property_with_key(css_key_border_bottom_style, "border-bottom-style", "none | solid", "none");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-left-style */
	css_register_property_with_key(css_key_border_left_style, "border-left-style", "none | solid", "none");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-top-left-radius */
	css_register_property_with_key(css_key_border_top_left_radius, "border-top-left-radius", "<length> | <percentage>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-top-right-radius */
	css_register_property_with_key(css_key_border_top_right_radius, "border-top-right-radius", "<length> | <percentage>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-bottom-left-radius */
	css_register_property_with_key(css_key_border_bottom_left_radius, "border-bottom-left-radius", "<length> | <percentage>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/border-bottom-right-radius */
	css_register_property_with_key(css_key_border_bottom_right_radius, "border-bottom-right-radius", "<length> | <percentage>", "0");

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_box_shadow, "box-shadow", "none | <shadow>", "none");


	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_pointer_events, "pointer-events" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_focusable, "focusable" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_box_sizing, "box-sizing" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_flex_basis, "flex-basis" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_flex_direction, "flex-direction" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_flex_grow, "flex-grow" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_flex_shrink, "flex-shrink" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_flex_wrap, "flex-wrap" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_justify_content, "justify-content" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_align_content, "align-content" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_align_items, "align-items" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_color, "color" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_font_family, "font-family" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_font_size, "font-size" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_font_style, "font-style" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_text_align, "text-align" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_line_height, "line-height" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_content, "content" },

	/** @see https://developer.mozilla.org/en-US/docs/Web/CSS/width */
	css_register_property_with_key(css_key_white_space, "white-space" }

}

static void css_destroy_properties(void)
{
	dict_destroy(css.property_map);
	css.property_map = NULL;
	css.properties = NULL;
	css.properties_length = 0;
}

static void css_init_keywords(void)
{
	static dict_type_t keys_dt = { 0 };
	static dict_type_t names_dt = { 0 };

	names_dt.key_compare = ikey_dict_key_compare;
	names_dt.hash_function = ikey_dict_hash;
	dict_init_string_key_type(&keys_dt);
	keys_dt.val_destructor = keyword_destructor;
	css.keywords = dict_create(&keys_dt, NULL);
	css.keyword_names = dict_create(&names_dt, NULL);

	css_register_keyword(CSS_KEYWORD_NONE, "none");
	css_register_keyword(CSS_KEYWORD_AUTO, "auto");
	css_register_keyword(CSS_KEYWORD_INHERIT, "inherit");
	css_register_keyword(CSS_KEYWORD_INITIAL, "initial");
	css_register_keyword(CSS_KEYWORD_CONTAIN, "contain");
	css_register_keyword(CSS_KEYWORD_COVER, "cover");
	css_register_keyword(CSS_KEYWORD_LEFT, "left");
	css_register_keyword(CSS_KEYWORD_CENTER, "center");
	css_register_keyword(CSS_KEYWORD_RIGHT, "right");
	css_register_keyword(CSS_KEYWORD_TOP, "top");
	css_register_keyword(CSS_KEYWORD_TOP_LEFT, "top left");
	css_register_keyword(CSS_KEYWORD_TOP_CENTER, "top center");
	css_register_keyword(CSS_KEYWORD_TOP_RIGHT, "top right");
	css_register_keyword(CSS_KEYWORD_MIDDLE, "middle");
	css_register_keyword(CSS_KEYWORD_CENTER_LEFT, "center left");
	css_register_keyword(CSS_KEYWORD_CENTER_CENTER, "center center");
	css_register_keyword(CSS_KEYWORD_CENTER_RIGHT, "center right");
	css_register_keyword(CSS_KEYWORD_BOTTOM, "bottom");
	css_register_keyword(CSS_KEYWORD_BOTTOM_LEFT, "bottom left");
	css_register_keyword(CSS_KEYWORD_BOTTOM_CENTER, "bottom center");
	css_register_keyword(CSS_KEYWORD_BOTTOM_RIGHT, "bottom right");
	css_register_keyword(CSS_KEYWORD_SOLID, "solid");
	css_register_keyword(CSS_KEYWORD_DOTTED, "dotted");
	css_register_keyword(CSS_KEYWORD_DOUBLE, "double");
	css_register_keyword(CSS_KEYWORD_DASHED, "dashed");
	css_register_keyword(CSS_KEYWORD_CONTENT_BOX, "content-box");
	css_register_keyword(CSS_KEYWORD_PADDING_BOX, "padding-box");
	css_register_keyword(CSS_KEYWORD_BORDER_BOX, "border-box");
	css_register_keyword(CSS_KEYWORD_GRAPH_BOX, "graph-box");
	css_register_keyword(CSS_KEYWORD_STATIC, "static");
	css_register_keyword(CSS_KEYWORD_RELATIVE, "relative");
	css_register_keyword(CSS_KEYWORD_ABSOLUTE, "absolute");
	css_register_keyword(CSS_KEYWORD_BLOCK, "block");
	css_register_keyword(CSS_KEYWORD_INLINE_BLOCK, "inline-block");
	css_register_keyword(CSS_KEYWORD_FLEX, "flex");
	css_register_keyword(CSS_KEYWORD_NORMAL, "normal");
	css_register_keyword(CSS_KEYWORD_FLEX_START, "flex-start");
	css_register_keyword(CSS_KEYWORD_FLEX_END, "flex-end");
	css_register_keyword(CSS_KEYWORD_STRETCH, "stretch");
	css_register_keyword(CSS_KEYWORD_SPACE_BETWEEN, "space-between");
	css_register_keyword(CSS_KEYWORD_SPACE_AROUND, "space-around");
	css_register_keyword(CSS_KEYWORD_SPACE_EVENLY, "space-evenly");
	css_register_keyword(CSS_KEYWORD_NOWRAP, "nowrap");
	css_register_keyword(CSS_KEYWORD_WRAP, "wrap");
	css_register_keyword(CSS_KEYWORD_ROW, "row");
	css_register_keyword(CSS_KEYWORD_COLUMN, "column");
}

static void css_destroy_keywords(void)
{
	dict_destroy(css.keyword_names);
	dict_destroy(css.keywords);
	css.keywords = NULL;
	css.keyword_names = NULL;
}

void css_init(void)
{
	css_keyword_t *skn, *skn_end;

	css.strpool = strpool_create();
	css_init_cache();
	css_init_keywords();
	css_init_properties();
	list_create(&css.groups);
	css.count = STYLE_KEY_TOTAL;
}

void css_destroy(void)
{
	css_destroy_cache();
	css_destroy_properties();
	css_destroy_keywords();
	list_destroy(&css.groups, (list_item_destructor_t)dict_destroy);
	strpool_destroy(css.strpool);
}
