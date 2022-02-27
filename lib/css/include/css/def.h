#ifndef LIBCSS_INCLUDE_CSS_DEF_H
#define LIBCSS_INCLUDE_CSS_DEF_H

#include <LCUI/types.h>
#include <LCUI/util.h>

#define CSS_SELECTOR_MAX_LEN 1024
#define CSS_SELECTOR_MAX_DEPTH 32

#define CASE_WHITE_SPACE \
	case ' ':        \
	case '\n':       \
	case '\r':       \
	case '\t'

typedef enum css_property_key_t {
	// position start
	css_key_left,
	css_key_right,
	css_key_top,
	css_key_bottom,
	css_key_position,
	// position end

	// display start
	css_key_visibility,
	css_key_display,
	// display end

	css_key_z_index,
	css_key_opacity,
	css_key_box_sizing,
	css_key_width,
	css_key_height,
	css_key_min_width,
	css_key_min_height,
	css_key_max_width,
	css_key_max_height,

	// margin start
	css_key_margin,
	css_key_margin_top,
	css_key_margin_right,
	css_key_margin_bottom,
	css_key_margin_left,
	// margin end

	// padding start
	css_key_padding,
	css_key_padding_top,
	css_key_padding_right,
	css_key_padding_bottom,
	css_key_padding_left,
	// padding end

	css_key_vertical_align,

	// border start
	css_key_border,
	css_key_border_top_width,
	css_key_border_top_style,
	css_key_border_top_color,
	css_key_border_right_width,
	css_key_border_right_style,
	css_key_border_right_color,
	css_key_border_bottom_width,
	css_key_border_bottom_style,
	css_key_border_bottom_color,
	css_key_border_left_width,
	css_key_border_left_style,
	css_key_border_left_color,
	css_key_border_top_left_radius,
	css_key_border_top_right_radius,
	css_key_border_bottom_left_radius,
	css_key_border_bottom_right_radius,
	// border end

	// background start
	css_key_background,
	css_key_background_color,
	css_key_background_image,
	css_key_background_size,
	css_key_background_size_width,
	css_key_background_size_height,
	css_key_background_repeat,
	css_key_background_repeat_x,
	css_key_background_repeat_y,
	css_key_background_position,
	css_key_background_position_x,
	css_key_background_position_y,
	css_key_background_origin,
	// background end

	css_key_box_shadow,

	// flex style start
	css_key_flex,
	css_key_flex_basis,
	css_key_flex_grow,
	css_key_flex_shrink,
	css_key_flex_direction,
	css_key_flex_wrap,
	css_key_justify_content,
	css_key_align_content,
	css_key_align_items,
	// flex style end

	css_key_color,
	css_key_font_size,
	css_key_font_style,
	css_key_font_weight,
	css_key_font_family,
	css_key_line_height,
	css_key_text_align,
	css_key_content,
	css_key_white_space,

	css_key_pointer_events,
	css_key_focusable,
	STYLE_KEY_TOTAL
} css_property_key_t;

#define css_key_flex_style_start css_key_flex_basis
#define css_key_flex_style_end css_key_align_content
#define css_key_position_start css_key_left
#define css_key_position_end css_key_position
#define css_key_margin_start css_key_margin_top
#define css_key_margin_end css_key_margin_left
#define css_key_padding_start css_key_padding_top
#define css_key_padding_end css_key_padding_left
#define css_key_border_start css_key_border_top_width
#define css_key_border_end css_key_border_bottom_right_radius
#define css_key_background_start css_key_background_color
#define css_key_background_end css_key_background_origin
#define css_key_box_shadow_start css_key_box_shadow_x
#define css_key_box_shadow_end css_key_box_shadow_color

/* FIXME: remove css_keyword_value_t
 * These values do not need to put in css_keyword_value_t, because they are not
 * strongly related and should be defined separately where they are needed.
 */

typedef enum css_keyword_value_t {
	CSS_KEYWORD_NONE,
	CSS_KEYWORD_AUTO,
	CSS_KEYWORD_NORMAL,
	CSS_KEYWORD_INHERIT,
	CSS_KEYWORD_INITIAL,
	CSS_KEYWORD_CONTAIN,
	CSS_KEYWORD_COVER,
	CSS_KEYWORD_LEFT,
	CSS_KEYWORD_CENTER,
	CSS_KEYWORD_RIGHT,
	CSS_KEYWORD_TOP,
	CSS_KEYWORD_TOP_LEFT,
	CSS_KEYWORD_TOP_CENTER,
	CSS_KEYWORD_TOP_RIGHT,
	CSS_KEYWORD_MIDDLE,
	CSS_KEYWORD_CENTER_LEFT,
	CSS_KEYWORD_CENTER_CENTER,
	CSS_KEYWORD_CENTER_RIGHT,
	CSS_KEYWORD_BOTTOM,
	CSS_KEYWORD_BOTTOM_LEFT,
	CSS_KEYWORD_BOTTOM_CENTER,
	CSS_KEYWORD_BOTTOM_RIGHT,
	CSS_KEYWORD_SOLID,
	CSS_KEYWORD_DOTTED,
	CSS_KEYWORD_DOUBLE,
	CSS_KEYWORD_DASHED,
	CSS_KEYWORD_CONTENT_BOX,
	CSS_KEYWORD_PADDING_BOX,
	CSS_KEYWORD_BORDER_BOX,
	CSS_KEYWORD_GRAPH_BOX,
	CSS_KEYWORD_STATIC,
	CSS_KEYWORD_RELATIVE,
	CSS_KEYWORD_ABSOLUTE,
	CSS_KEYWORD_BLOCK,
	CSS_KEYWORD_INLINE_BLOCK,
	CSS_KEYWORD_FLEX,
	CSS_KEYWORD_FLEX_START,
	CSS_KEYWORD_FLEX_END,
	CSS_KEYWORD_STRETCH,
	CSS_KEYWORD_SPACE_BETWEEN,
	CSS_KEYWORD_SPACE_AROUND,
	CSS_KEYWORD_SPACE_EVENLY,
	CSS_KEYWORD_WRAP,
	CSS_KEYWORD_NOWRAP,
	CSS_KEYWORD_ROW,
	CSS_KEYWORD_COLUMN
} css_keyword_value_t;

typedef enum css_style_value_type_t {
	CSS_NO_VALUE,
	CSS_INVALID_VALUE,
	CSS_UNPARSED_VALUE,
	CSS_ARRAY_VALUE,

	CSS_NUMBERIC_VALUE,
	CSS_STRING_VALUE,
	CSS_KEYWORD_VALUE,
	CSS_COLOR_VALUE,
	CSS_IMAGE_VALUE,

	CSS_UNIT_VALUE,
	CSS_LENGTH_VALUE,
	CSS_PERCENTAGE_VALUE,
} css_style_value_type_t;

typedef char* css_image_value_t;
typedef char* css_unparsed_value_t;
typedef char* css_string_value_t;
typedef int32_t css_integer_value_t;
typedef double css_numberic_value_t;

typedef union css_color_value_t {
	uint32_t value;
	struct {
		unsigned char r;
		unsigned char g;
		unsigned char b;
		unsigned char a;
	};
} css_color_value_t;

typedef void *css_private_value_t;

/** https://developer.mozilla.org/en-US/docs/Web/API/CSSUnitValue */
typedef struct css_unit_value_t {
	css_numberic_value_t value;
	char unit[4];
} css_unit_value_t;

typedef struct css_style_value_t css_style_value_t;
typedef css_style_value_t * css_style_array_value_t;

/** https://developer.mozilla.org/en-US/docs/Web/API/CSSStyleValue */
struct css_style_value_t {
	css_style_value_type_t type;
	union {
		css_private_value_t value;
		css_numberic_value_t numberic_value;
		css_integer_value_t integer_value;
		css_string_value_t string_value;
		css_unit_value_t unit_value;
		css_color_value_t color_value;
		css_image_value_t image_value;
		css_unparsed_value_t unparsed_value;
		css_keyword_value_t keyword_value;
		css_style_array_value_t array_value;
	};
};

struct css_style_declaration_t {
	css_style_value_t *sheet;
	size_t length;
};

typedef struct css_style_declaration_t css_style_declaration_t;
typedef list_t css_style_properties_t;

typedef css_style_properties_t css_style_props_t;
typedef css_style_declaration_t css_style_decl_t;
typedef unsigned css_selector_hash_t;

typedef struct css_style_property_t {
	css_property_key_t key;
	css_style_value_t style;
	list_node_t node;
} css_style_property_t;

typedef struct css_selector_node_t {
	char *id;
	char *type;
	char **classes;
	char **status;
	char *fullname; /**< 全名，由 id、type、classes、status 组合而成 */
	int rank;
} css_selector_node_t;

typedef struct css_selector_t {
	int rank;                    /**< 权值，决定优先级 */
	int batch_num;               /**< 批次号 */
	int length;                  /**< 选择器结点长度 */
	css_selector_hash_t hash;    /**< 哈希值 */
	css_selector_node_t **nodes; /**< 选择器结点列表 */
} css_selector_t;

typedef enum css_font_style_t {
	CSS_FONT_STYLE_NORMAL,
	CSS_FONT_STYLE_ITALIC,
	CSS_FONT_STYLE_OBLIQUE,
	CSS_FONT_STYLE_TOTAL_NUM
} css_font_style_t;

typedef enum css_font_weight_t {
	CSS_FONT_WEIGHT_NONE = 0,
	CSS_FONT_WEIGHT_THIN = 100,
	CSS_FONT_WEIGHT_EXTRA_LIGHT = 200,
	CSS_FONT_WEIGHT_LIGHT = 300,
	CSS_FONT_WEIGHT_NORMAL = 400,
	CSS_FONT_WEIGHT_MEDIUM = 500,
	CSS_FONT_WEIGHT_SEMI_BOLD = 600,
	CSS_FONT_WEIGHT_BOLD = 700,
	CSS_FONT_WEIGHT_EXTRA_BOLD = 800,
	CSS_FONT_WEIGHT_BLACK = 900,
	CSS_FONT_WEIGHT_TOTAL_NUM = 9
} css_font_weight_t;

typedef struct css_font_face_t {
	char *font_family;
	css_font_style_t font_style;
	css_font_weight_t font_weight;
	char *src;
} css_font_face_t;

typedef struct css_syntax_t {
	css_style_value_type_t types[8];
	unsigned length;
} css_syntax_t;

typedef struct css_property_definition_t {
	int key;
	const char *name;
	css_syntax_t syntax;
	css_style_value_t initial_value;
} css_property_definition_t;

#endif
