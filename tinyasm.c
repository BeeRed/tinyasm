/*
 ** TinyASM - 8086/8088 assembler for DOS
 **
 ** by Oscar Toledo G.
 **
 ** Creation date: Oct/01/2019.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*#define DEBUG*/

#define MAX_SIZE        256

/* struct hold global vars to avoid shadow with func local vars see: -Wshadow */
struct _tinyasm_s {
    char    *input_filename;
    char    *output_filename;
    char    *listing_filename;

    int     line_number;
    FILE    *output;
    FILE    *listing;

    char    line[MAX_SIZE];
    char    part[MAX_SIZE];
    char    name[MAX_SIZE];
    char    expr_name[MAX_SIZE];
    char    undefined_name[MAX_SIZE];

    char    global_label[MAX_SIZE];
    char    *prev_p;

    char    *p;
    char    *g;
    char    generated[8];

    int     assembler_step;
    int     default_start_address;
    int     start_address;
    int     address;
    int     first_time;

    int     errors;
    int     warnings;
    int     bytes;
    int     change;
    int     change_number;

    int     instruction_addressing;
    int     instruction_offset;
    int     instruction_offset_width;

    int     instruction_register;

    int     instruction_value;
    int     instruction_value2;
};
typedef struct _tinyasm_s tinyasm_t;
tinyasm_t   tAsm;

struct label {
    struct label *left;
    struct label *right;
    int value;
    char name[1];
};

struct label *label_list;
struct label *last_label;
int undefined;

extern char *instruction_set[];

char *reg1[16] = {
    "AL",
    "CL",
    "DL",
    "BL",
    "AH",
    "CH",
    "DH",
    "BH",
    "AX",
    "CX",
    "DX",
    "BX",
    "SP",
    "BP",
    "SI",
    "DI"
};

/* function prototypes */

void message(int error, char *message);
char *match_addressing       (char *p, int width);
char *match_register         (char *p, int width, int *value);
char *match_expression       (char *p, int *value);
char *match_expression_level1(char *p, int *value);
char *match_expression_level2(char *p, int *value);
char *match_expression_level3(char *p, int *value);
char *match_expression_level4(char *p, int *value);
char *match_expression_level5(char *p, int *value);
char *match_expression_level6(char *p, int *value);

struct label    *define_label(char *name, int value);
struct label    *find_label(char *name);
void    sort_labels(struct label *node);
char    *avoid_spaces(char *p);
int     islabel(int c);
char    *read_character(char *p, int *c);
void    emit_byte(int byte);
char    *match(char *p, char *pattern, char *decode);
void    to_lowercase(char *p);
void    separate(void);
void    check_end(char *p);
void    process_instruction(void);
void    reset_address(void);
void    incbin(char *fname);
void    do_assembly(char *fname);


#ifdef __DESMET__
/* Work around bug in DeSmet 3.1N runtime: closeall() overflows buffer and clobbers exit status */
#define exit(status)    _exit(status)
#endif

/*
 ** Define a new label
 */
struct label *define_label(char *name, int value)
{
    struct label *label;
    struct label *explore;
    int c;

    /* Allocate label */
    label = malloc(sizeof(struct label) + strlen(name));
    if (label == NULL) {
        fprintf(stderr, "Out of memory for label\n");
        exit(1);
        return NULL;
    }

    /* Fill label */
    label->left = NULL;
    label->right = NULL;
    label->value = value;
    strcpy(label->name, name);

    /* Populate binary tree */
    if (label_list == NULL) {
        label_list = label;
    } else {
        explore = label_list;
        while (1) {
            c = strcmp(label->name, explore->name);
            if (c < 0) {
                if (explore->left == NULL) {
                    explore->left = label;
                    break;
                }
                explore = explore->left;
            } else if (c > 0) {
                if (explore->right == NULL) {
                    explore->right = label;
                    break;
                }
                explore = explore->right;
            }
        }
    }
    return label;
}

/*
 ** Find a label
 */
struct label *find_label(char *name)
{
    struct label *explore;
    int c;

    /* Follows a binary tree */
    explore = label_list;
    while (explore != NULL) {
        c = strcmp(name, explore->name);
        if (c == 0)
            return explore;
        if (c < 0)
            explore = explore->left;
        else
            explore = explore->right;
    }
    return NULL;
}

/*
 ** Sort recursively labels (already done by binary tree)
 */
void sort_labels(struct label *node)
{
    if (node->left != NULL)
        sort_labels(node->left);
    fprintf(tAsm.listing, "%-20s %04x\n", node->name, node->value);
    if (node->right != NULL)
        sort_labels(node->right);
}

/*
 ** Avoid spaces in input
 */
char *avoid_spaces(char *p)
{
    while (isspace(*p))
        p++;
    return p;
}

/*
 ** Match addressing
 */
char *match_addressing(char *p, int width)
{
    int reg;
    int reg2;
    char *p2;
    int *bits;

    bits = &tAsm.instruction_addressing;
    tAsm.instruction_offset = 0;
    tAsm.instruction_offset_width = 0;

    p = avoid_spaces(p);
    if (*p == '[') {
        p = avoid_spaces(p + 1);
        p2 = match_register(p, 16, &reg);
        if (p2 != NULL) {
            p = avoid_spaces(p2);
            if (*p == ']') {
                p++;
                if (reg == 3) {   /* BX */
                    *bits = 0x07;
                } else if (reg == 5) {  /* BP */
                    *bits = 0x46;
                    tAsm.instruction_offset = 0;
                    tAsm.instruction_offset_width = 1;
                } else if (reg == 6) {  /* SI */
                    *bits = 0x04;
                } else if (reg == 7) {  /* DI */
                    *bits = 0x05;
                } else {    /* Not valid */
                    return NULL;
                }
            } else if (*p == '+' || *p == '-') {
                if (*p == '+') {
                    p = avoid_spaces(p + 1);
                    p2 = match_register(p, 16, &reg2);
                } else {
                    p2 = NULL;
                }
                if (p2 != NULL) {
                    if ((reg == 3 && reg2 == 6) || (reg == 6 && reg2 == 3)) {   /* BX+SI / SI+BX */
                        *bits = 0x00;
                    } else if ((reg == 3 && reg2 == 7) || (reg == 7 && reg2 == 3)) {    /* BX+DI / DI+BX */
                        *bits = 0x01;
                    } else if ((reg == 5 && reg2 == 6) || (reg == 6 && reg2 == 5)) {    /* BP+SI / SI+BP */
                        *bits = 0x02;
                    } else if ((reg == 5 && reg2 == 7) || (reg == 7 && reg2 == 5)) {    /* BP+DI / DI+BP */
                        *bits = 0x03;
                    } else {    /* Not valid */
                        return NULL;
                    }
                    p = avoid_spaces(p2);
                    if (*p == ']') {
                        p++;
                    } else if (*p == '+' || *p == '-') {
                        p2 = match_expression(p, &tAsm.instruction_offset);
                        if (p2 == NULL)
                            return NULL;
                        p = avoid_spaces(p2);
                        if (*p != ']')
                            return NULL;
                        p++;
                        if (tAsm.instruction_offset >= -0x80 && tAsm.instruction_offset <= 0x7f) {
                            tAsm.instruction_offset_width = 1;
                            *bits |= 0x40;
                        } else {
                            tAsm.instruction_offset_width = 2;
                            *bits |= 0x80;
                        }
                    } else {    /* Syntax error */
                        return NULL;
                    }
                } else {
                    if (reg == 3) {   /* BX */
                        *bits = 0x07;
                    } else if (reg == 5) {  /* BP */
                        *bits = 0x06;
                    } else if (reg == 6) {  /* SI */
                        *bits = 0x04;
                    } else if (reg == 7) {  /* DI */
                        *bits = 0x05;
                    } else {    /* Not valid */
                        return NULL;
                    }
                    p2 = match_expression(p, &tAsm.instruction_offset);
                    if (p2 == NULL)
                        return NULL;
                    p = avoid_spaces(p2);
                    if (*p != ']')
                        return NULL;
                    p++;
                    if (tAsm.instruction_offset >= -0x80 && tAsm.instruction_offset <= 0x7f) {
                        tAsm.instruction_offset_width = 1;
                        *bits |= 0x40;
                    } else {
                        tAsm.instruction_offset_width = 2;
                        *bits |= 0x80;
                    }
                }
            } else {    /* Syntax error */
                return NULL;
            }
        } else {    /* No valid register, try expression (absolute addressing) */
            p2 = match_expression(p, &tAsm.instruction_offset);
            if (p2 == NULL)
                return NULL;
            p = avoid_spaces(p2);
            if (*p != ']')
                return NULL;
            p++;
            *bits = 0x06;
            tAsm.instruction_offset_width = 2;
        }
    } else {    /* Register */
        p = match_register(p, width, &reg);
        if (p == NULL)
            return NULL;
        *bits = 0xc0 | reg;
    }
    return p;
}

/*
 ** Check for a label character
 */
int islabel(int c)
{
    return isalpha(c) || isdigit(c) || c == '_' || c == '.';
}

/*
 ** Match register
 */
char *match_register(char *p, int width, int *value)
{
    char reg[3];
    int c;

    p = avoid_spaces(p);
    if (!isalpha(p[0]) || !isalpha(p[1]) || islabel(p[2]))
        return NULL;
    reg[0] = p[0];
    reg[1] = p[1];
    reg[2] = '\0';
    if (width == 8) {   /* 8-bit */
        for (c = 0; c < 8; c++)
            if (strcmp(reg, reg1[c]) == 0)
                break;
        if (c < 8) {
            *value = c;
            return p + 2;
        }
    } else {    /* 16-bit */
        for (c = 0; c < 8; c++)
            if (strcmp(reg, reg1[c + 8]) == 0)
                break;
        if (c < 8) {
            *value = c;
            return p + 2;
        }
    }
    return NULL;
}

/*
 ** Read character for string or character literal
 */
char *read_character(char *p, int *c)
{
    if (*p == '\\') {
        p++;
        if (*p == '\'') {
            *c = '\'';
            p++;
        } else if (*p == '\"') {
            *c = '"';
            p++;
        } else if (*p == '\\') {
            *c = '\\';
            p++;
        } else if (*p == 'a') {
            *c = 0x07;
            p++;
        } else if (*p == 'b') {
            *c = 0x08;
            p++;
        } else if (*p == 't') {
            *c = 0x09;
            p++;
        } else if (*p == 'n') {
            *c = 0x0a;
            p++;
        } else if (*p == 'v') {
            *c = 0x0b;
            p++;
        } else if (*p == 'f') {
            *c = 0x0c;
            p++;
        } else if (*p == 'r') {
            *c = 0x0d;
            p++;
        } else if (*p == 'e') {
            *c = 0x1b;
            p++;
        } else if (*p >= '0' && *p <= '7') {
            *c = 0;
            while (*p >= '0' && *p <= '7') {
                *c = *c * 8 + (*p - '0');
                p++;
            }
        } else {
            p--;
            *c = *p;
            p++;
            message(1, "bad escape inside string");
        }
    } else {
        *c = *p;
        p++;
    }
    return p;
}

/*
 ** Match expression (top tier)
 */
char *match_expression(char *p, int *value)
{
    int value1;

    p = match_expression_level1(p, value);
    if (p == NULL)
        return NULL;
    while (1) {
        p = avoid_spaces(p);
        if (*p == '|') {    /* Binary OR */
            p++;
            value1 = *value;
            p = match_expression_level1(p, value);
            if (p == NULL)
                return NULL;
            *value |= value1;
        } else {
            return p;
        }
    }
}

/*
 ** Match expression
 */
char *match_expression_level1(char *p, int *value)
{
    int value1;

    p = match_expression_level2(p, value);
    if (p == NULL)
        return NULL;
    while (1) {
        p = avoid_spaces(p);
        if (*p == '^') {    /* Binary XOR */
            p++;
            value1 = *value;
            p = match_expression_level2(p, value);
            if (p == NULL)
                return NULL;
            *value ^= value1;
        } else {
            return p;
        }
    }
}

/*
 ** Match expression
 */
char *match_expression_level2(char *p, int *value)
{
    int value1;

    p = match_expression_level3(p, value);
    if (p == NULL)
        return NULL;
    while (1) {
        p = avoid_spaces(p);
        if (*p == '&') {    /* Binary AND */
            p++;
            value1 = *value;
            p = match_expression_level3(p, value);
            if (p == NULL)
                return NULL;
            *value &= value1;
        } else {
            return p;
        }
    }
}

/*
 ** Match expression
 */
char *match_expression_level3(char *p, int *value)
{
    int value1;

    p = match_expression_level4(p, value);
    if (p == NULL)
        return NULL;
    while (1) {
        p = avoid_spaces(p);
        if (*p == '<' && p[1] == '<') { /* Shift to left */
            p += 2;
            value1 = *value;
            p = match_expression_level4(p, value);
            if (p == NULL)
                return NULL;
            *value = value1 << *value;
        } else if (*p == '>' && p[1] == '>') {  /* Shift to right */
            p += 2;
            value1 = *value;
            p = match_expression_level4(p, value);
            if (p == NULL)
                return NULL;
            *value = value1 >> *value;
        } else {
            return p;
        }
    }
}

/*
 ** Match expression
 */
char *match_expression_level4(char *p, int *value)
{
    int value1;

    p = match_expression_level5(p, value);
    if (p == NULL)
        return NULL;
    while (1) {
        p = avoid_spaces(p);
        if (*p == '+') {    /* Add operator */
            p++;
            value1 = *value;
            p = match_expression_level5(p, value);
            if (p == NULL)
                return NULL;
            *value = value1 + *value;
        } else if (*p == '-') { /* Subtract operator */
            p++;
            value1 = *value;
            p = match_expression_level5(p, value);
            if (p == NULL)
                return NULL;
            *value = value1 - *value;
        } else {
            return p;
        }
    }
}

/*
 ** Match expression
 */
char *match_expression_level5(char *p, int *value)
{
    int value1;

    p = match_expression_level6(p, value);
    if (p == NULL)
        return NULL;
    while (1) {
        p = avoid_spaces(p);
        if (*p == '*') {    /* Multiply operator */
            p++;
            value1 = *value;
            p = match_expression_level6(p, value);
            if (p == NULL)
                return NULL;
            *value = value1 * *value;
        } else if (*p == '/') { /* Division operator */
            p++;
            value1 = *value;
            p = match_expression_level6(p, value);
            if (p == NULL)
                return NULL;
            if (*value == 0) {
                if (tAsm.assembler_step == 2)
                    message(1, "division by zero");
                *value = 1;
            }
            *value = (unsigned) value1 / *value;
        } else if (*p == '%') { /* Modulo operator */
            p++;
            value1 = *value;
            p = match_expression_level6(p, value);
            if (p == NULL)
                return NULL;
            if (*value == 0) {
                if (tAsm.assembler_step == 2)
                    message(1, "modulo by zero");
                *value = 1;
            }
            *value = value1 % *value;
        } else {
            return p;
        }
    }
}

/*
 ** Match expression (bottom tier)
 */
char *match_expression_level6(char *p, int *value)
{
    int number;
    int c;
    char *p2;
    struct label *label;

    p = avoid_spaces(p);
    if (*p == '(') {    /* Handle parenthesized expressions */
        p++;
        p = match_expression(p, value);
        if (p == NULL)
            return NULL;
        p = avoid_spaces(p);
        if (*p != ')')
            return NULL;
        p++;
        return p;
    }
    if (*p == '-') {    /* Simple negation */
        p++;
        p = match_expression_level6(p, value);
        if (p == NULL)
            return NULL;
        *value = -*value;
        return p;
    }
    if (*p == '+') {    /* Unary */
        p++;
        p = match_expression_level6(p, value);
        if (p == NULL)
            return NULL;
        return p;
    }
    if (p[0] == '0' && tolower(p[1]) == 'b') {  /* Binary */
        p += 2;
        number = 0;
        while (p[0] == '0' || p[0] == '1' || p[0] == '_') {
            if (p[0] != '_') {
                number <<= 1;
                if (p[0] == '1')
                    number |= 1;
            }
            p++;
        }
        *value = number;
        return p;
    }
    if (p[0] == '0' && tolower(p[1]) == 'x' && isxdigit(p[2])) {    /* Hexadecimal */
        p += 2;
        number = 0;
        while (isxdigit(p[0])) {
            c = toupper(p[0]);
            c = c - '0';
            if (c > 9)
                c -= 7;
            number = (number << 4) | c;
            p++;
        }
        *value = number;
        return p;
    }
    if (p[0] == '$' && isdigit(p[1])) {    /* Hexadecimal */
        /* This is nasm syntax, notice no letter is allowed after $ */
        /* So it's preferrable to use prefix 0x for hexadecimal */
        p += 1;
        number = 0;
        while (isxdigit(p[0])) {
            c = toupper(p[0]);
            c = c - '0';
            if (c > 9)
                c -= 7;
            number = (number << 4) | c;
            p++;
        }
        *value = number;
        return p;
    }
    if (p[0] == '\'') { /* Character constant */
        p++;
        p = read_character(p, value);
        if (p[0] != '\'') {
            message(1, "Missing apostrophe");
        } else {
            p++;
        }
        return p;
    }
    if (isdigit(*p)) {   /* Decimal */
        number = 0;
        while (isdigit(p[0])) {
            c = p[0] - '0';
            number = number * 10 + c;
            p++;
        }
        *value = number;
        return p;
    }
    if (*p == '$' && p[1] == '$') { /* Start address */
        p += 2;
        *value = tAsm.start_address;
        return p;
    }
    if (*p == '$') { /* Current address */
        p++;
        *value = tAsm.address;
        return p;
    }
    if (isalpha(*p) || *p == '_' || *p == '.') { /* Label */
        if (*p == '.') {
            strcpy(tAsm.expr_name, tAsm.global_label);
            p2 = tAsm.expr_name;
            while (*p2)
                p2++;
        } else {
            p2 = tAsm.expr_name;
        }
        while (isalpha(*p) || isdigit(*p) || *p == '_' || *p == '.')
            *p2++ = *p++;
        *p2 = '\0';
        for (c = 0; c < 16; c++)
            if (strcmp(tAsm.expr_name, reg1[c]) == 0)
                return NULL;
        label = find_label(tAsm.expr_name);
        if (label == NULL) {
            *value = 0;
            undefined++;
            strcpy(tAsm.undefined_name, tAsm.expr_name);
        } else {
            *value = label->value;
        }
        return p;
    }
    return NULL;
}

/*
 ** Emit one byte to output
 */
void emit_byte(int byte)
{
    char buf[1];

    if (tAsm.assembler_step == 2) {
        if (tAsm.g != NULL && tAsm.g < tAsm.generated + sizeof(tAsm.generated))
            *tAsm.g++ = byte;
        buf[0] = byte;
        /* Cannot use fputc because DeSmet C expands to CR LF */
        fwrite(buf, 1, 1, tAsm.output);
        tAsm.bytes++;
    }
    tAsm.address++;
}

/*
 ** Search for a match with instruction
 */
char *match(char *p, char *pattern, char *decode)
{
    char *p2;
    int c;
    int d;
    int bit;
    int qualifier;
    char *base;

    undefined = 0;
    while (*pattern) {
/*        fputc(*pattern, stdout);*/
        if (*pattern == '%') {    /* Special */
            pattern++;
            if (*pattern == 'd') {  /* Addressing */
                pattern++;
                qualifier = 0;
                if (memcmp(p, "WORD", 4) == 0 && !isalpha(p[4])) {
                    p = avoid_spaces(p + 4);
                    if (*p != '[')
                        return NULL;
                    qualifier = 16;
                } else if (memcmp(p, "BYTE", 4) == 0 && !isalpha(p[4])) {
                    p = avoid_spaces(p + 4);
                    if (*p != '[')
                        return NULL;
                    qualifier = 8;
                }
                if (*pattern == 'w') {
                    pattern++;
                    if (qualifier != 16 && match_register(p, 16, &d) == 0)
                        return NULL;
                } else if (*pattern == 'b') {
                    pattern++;
                    if (qualifier != 8 && match_register(p, 8, &d) == 0)
                        return NULL;
                } else {
                    if (qualifier == 8 && *pattern != '8')
                        return NULL;
                    if (qualifier == 16 && *pattern != '1')
                        return NULL;
                }
                if (*pattern == '8') {
                    pattern++;
                    p2 = match_addressing(p, 8);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else if (*pattern == '1' && pattern[1] == '6') {
                    pattern += 2;
                    p2 = match_addressing(p, 16);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else {
                    return NULL;
                }
            } else if (*pattern == 'r') {   /* Register */
                pattern++;
                if (*pattern == '8') {
                    pattern++;
                    p2 = match_register(p, 8, &tAsm.instruction_register);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else if (*pattern == '1' && pattern[1] == '6') {
                    pattern += 2;
                    p2 = match_register(p, 16, &tAsm.instruction_register);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else {
                    return NULL;
                }
            } else if (*pattern == 'i') {   /* Immediate */
                pattern++;
                if (*pattern == '8') {
                    pattern++;
                    p2 = match_expression(p, &tAsm.instruction_value);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else if (*pattern == '1' && pattern[1] == '6') {
                    pattern += 2;
                    p2 = match_expression(p, &tAsm.instruction_value);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else {
                    return NULL;
                }
            } else if (*pattern == 'a') {   /* Address for jump */
                pattern++;
                if (*pattern == '8') {
                    pattern++;
                    p = avoid_spaces(p);
                    qualifier = 0;
                    if (memcmp(p, "SHORT", 5) == 0 && isspace(p[5])) {
                        p += 5;
                        qualifier = 1;
                    }
                    p2 = match_expression(p, &tAsm.instruction_value);
                    if (p2 == NULL)
                        return NULL;
                    if (qualifier == 0) {
                        c = tAsm.instruction_value - (tAsm.address + 2);
                        if (undefined == 0 && (c < -128 || c > 127) && memcmp(decode, "xeb", 3) == 0)
                            return NULL;
                    }
                    p = p2;
                } else if (*pattern == '1' && pattern[1] == '6') {
                    pattern += 2;
                    p = avoid_spaces(p);
                    if (memcmp(p, "SHORT", 5) == 0 && isspace(p[5]))
                        p2 = NULL;
                    else
                        p2 = match_expression(p, &tAsm.instruction_value);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else {
                    return NULL;
                }
            } else if (*pattern == 's') {   /* Signed immediate */
                pattern++;
                if (*pattern == '8') {
                    pattern++;
                    p = avoid_spaces(p);
                    qualifier = 0;
                    if (memcmp(p, "BYTE", 4) == 0 && isspace(p[4])) {
                        p += 4;
                        qualifier = 1;
                    }
                    p2 = match_expression(p, &tAsm.instruction_value);
                    if (p2 == NULL)
                        return NULL;
                    if (qualifier == 0) {
                        c = tAsm.instruction_value;
                        if (undefined != 0)
                            return NULL;
                        if (undefined == 0 && (c < -128 || c > 127))
                            return NULL;
                    }
                    p = p2;
                } else {
                    return NULL;
                }
            } else if (*pattern == 'f') {   /* FAR pointer */
                pattern++;
                if (*pattern == '3' && pattern[1] == '2') {
                    pattern += 2;
                    p2 = match_expression(p, &tAsm.instruction_value2);
                    if (p2 == NULL)
                        return NULL;
                    if (*p2 != ':')
                        return NULL;
                    p = p2 + 1;
                    p2 = match_expression(p, &tAsm.instruction_value);
                    if (p2 == NULL)
                        return NULL;
                    p = p2;
                } else {
                    return NULL;
                }
            } else {
                return NULL;
            }
            continue;
        }
        if (toupper(*p) != *pattern)
            return NULL;
        p++;
        if (*pattern == ',')    /* Allow spaces after comma */
            p = avoid_spaces(p);
        pattern++;
    }

    /*
     ** Instruction properly matched, now generate binary
     */
    base = decode;
    while (*decode) {
        decode = avoid_spaces(decode);
        if (decode[0] == 'x') { /* Byte */
            c = toupper(decode[1]);
            c -= '0';
            if (c > 9)
                c -= 7;
            d = toupper(decode[2]);
            d -= '0';
            if (d > 9)
                d -= 7;
            c = (c << 4) | d;
            emit_byte(c);
            decode += 3;
        } else {    /* Binary */
            if (*decode == 'b')
                decode++;
            bit = 0;
            c = 0;
            d = 0;
            while (bit < 8) {
                if (decode[0] == '0') { /* Zero */
                    decode++;
                    bit++;
                } else if (decode[0] == '1') {  /* One */
                    c |= 0x80 >> bit;
                    decode++;
                    bit++;
                } else if (decode[0] == '%') {  /* Special */
                    decode++;
                    if (decode[0] == 'r') { /* Register field */
                        decode++;
                        if (decode[0] == '8')
                            decode++;
                        else if (decode[0] == '1' && decode[1] == '6')
                            decode += 2;
                        c |= tAsm.instruction_register << (5 - bit);
                        bit += 3;
                    } else if (decode[0] == 'd') {  /* Addressing field */
                        if (decode[1] == '8')
                            decode += 2;
                        else
                            decode += 3;
                        if (bit == 0) {
                            c |= tAsm.instruction_addressing & 0xc0;
                            bit += 2;
                        } else {
                            c |= tAsm.instruction_addressing & 0x07;
                            bit += 3;
                            d = 1;
                        }
                    } else if (decode[0] == 'i' || decode[0] == 's') {
                        if (decode[1] == '8') {
                            decode += 2;
                            c = tAsm.instruction_value;
                            break;
                        } else {
                            decode += 3;
                            c = tAsm.instruction_value;
                            tAsm.instruction_offset = tAsm.instruction_value >> 8;
                            tAsm.instruction_offset_width = 1;
                            d = 1;
                            break;
                        }
                    } else if (decode[0] == 'a') {
                        if (decode[1] == '8') {
                            decode += 2;
                            c = tAsm.instruction_value - (tAsm.address + 1);
                            if (tAsm.assembler_step == 2 && (c < -128 || c > 127))
                                message(1, "short jump too long");
                            break;
                        } else {
                            decode += 3;
                            c = tAsm.instruction_value - (tAsm.address + 2);
                            tAsm.instruction_offset = c >> 8;
                            tAsm.instruction_offset_width = 1;
                            d = 1;
                            break;
                        }
                    } else if (decode[0] == 'f') {
                        decode += 3;
                        emit_byte(tAsm.instruction_value);
                        c = tAsm.instruction_value >> 8;
                        tAsm.instruction_offset = tAsm.instruction_value2;
                        tAsm.instruction_offset_width = 2;
                        d = 1;
                        break;
                    } else {
                        fprintf(stderr, "decode: internal error 2\n");
                    }
                } else {
                    fprintf(stderr, "decode: internal error 1 (%s)\n", base);
                    break;
                }
            }
            emit_byte(c);
            if (d == 1) {
                d = 0;
                if (tAsm.instruction_offset_width >= 1) {
                    emit_byte(tAsm.instruction_offset);
                }
                if (tAsm.instruction_offset_width >= 2) {
                    emit_byte(tAsm.instruction_offset >> 8);
                }
            }
        }
    }
    if (tAsm.assembler_step == 2) {
        if (undefined) {
            fprintf(stderr, "Error: undefined label '%s' at line %d\n", tAsm.undefined_name, tAsm.line_number);
        }
    }
    return p;
}

/*
 ** Make a string lowercase
 */
void to_lowercase(char *p)
{
    while (*p) {
        *p = tolower(*p);
        p++;
    }
}

/*
 ** Separate a portion of entry up to the first space
 */
void separate(void)
{
    char *p2;

    while (*tAsm.p && isspace(*tAsm.p))
        tAsm.p++;
    tAsm.prev_p = tAsm.p;
    p2 = tAsm.part;
    while (*tAsm.p && !isspace(*tAsm.p) && *tAsm.p != ';')
        *p2++ = *tAsm.p++;
    *p2 = '\0';
    while (*tAsm.p && isspace(*tAsm.p))
        tAsm.p++;
}

/*
 ** Check for end of line
 */
void check_end(char *p)
{
    p = avoid_spaces(p);
    if (*p && *p != ';') {
        fprintf(stderr, "Error: extra characters at end of line %d\n", tAsm.line_number);
        tAsm.errors++;
    }
}

/*
 ** Generate a message
 */
void message(int error, char *message)
{
    if (error) {
        fprintf(stderr, "Error: %s at line %d\n", message, tAsm.line_number);
        tAsm.errors++;
    } else {
        fprintf(stderr, "Warning: %s at line %d\n", message, tAsm.line_number);
        tAsm.warnings++;
    }
    if (tAsm.listing != NULL) {
        if (error) {
            fprintf(tAsm.listing, "Error: %s at line %d\n", message, tAsm.line_number);
        } else {
            fprintf(tAsm.listing, "Warning: %s at line %d\n", message, tAsm.line_number);
        }
    }
}

/*
 ** Process an instruction
 */
void process_instruction(void)
{
    char *p2 = NULL;
    char *p3;
    int c;

    if (strcmp(tAsm.part, "DB") == 0) {  /* Define byte */
        while (1) {
            tAsm.p = avoid_spaces(tAsm.p);
            if (*tAsm.p == '"') {    /* ASCII text */
                tAsm.p++;
                while (*tAsm.p && *tAsm.p != '"') {
                    tAsm.p = read_character(tAsm.p, &c);
                    emit_byte(c);
                }
                if (*tAsm.p) {
                    tAsm.p++;
                } else {
                    fprintf(stderr, "Error: unterminated string at line %d\n", tAsm.line_number);
                }
            } else {
                undefined = 0;
                p2 = match_expression(tAsm.p, &tAsm.instruction_value);
                if (p2 == NULL) {
                    fprintf(stderr, "Error: bad expression at line %d\n", tAsm.line_number);
                    break;
                } else if (tAsm.assembler_step == 2 && undefined) {
                    fprintf(stderr, "Error: undefined label '%s' at line %d\n", tAsm.undefined_name, tAsm.line_number);
                    break;
                }
                emit_byte(tAsm.instruction_value);
                tAsm.p = p2;
            }
            tAsm.p = avoid_spaces(tAsm.p);
            if (*tAsm.p == ',') {
                tAsm.p++;
                continue;
            }
            check_end(tAsm.p);
            break;
        }
        return;
    }
    if (strcmp(tAsm.part, "DW") == 0) {  /* Define word */
        while (1) {
            undefined = 0;
            p2 = match_expression(tAsm.p, &tAsm.instruction_value);
            if (p2 == NULL) {
                fprintf(stderr, "Error: bad expression at line %d\n", tAsm.line_number);
                break;
            } else if (tAsm.assembler_step == 2 && undefined) {
                fprintf(stderr, "Error: undefined label '%s' at line %d\n", tAsm.undefined_name, tAsm.line_number);
                break;
            }
            emit_byte(tAsm.instruction_value);
            emit_byte(tAsm.instruction_value >> 8);
            tAsm.p = avoid_spaces(p2);
            if (*tAsm.p == ',') {
                tAsm.p++;
                continue;
            }
            check_end(tAsm.p);
            break;
        }
        return;
    }
    while (tAsm.part[0]) {   /* Match against instruction set */
        c = 0;
        while (instruction_set[c] != NULL) {
            if (strcmp(tAsm.part, instruction_set[c]) == 0) {
                p2 = instruction_set[c];
                while (*p2++) ;
                p3 = p2;
                while (*p3++) ;

                p2 = match(tAsm.p, p2, p3);
                if (p2 != NULL) {
                    tAsm.p = p2;
                    break;
                }
            }
            c++;
        }
        if (instruction_set[c] == NULL) {
            char m[25 + MAX_SIZE];

            sprintf(m, "Undefined instruction '%s %s'", tAsm.part, tAsm.p);
            message(1, m);
            break;
        } else {
            tAsm.p = p2;
            separate();
        }
    }
}

/*
 ** Reset current address.
 ** Called anytime the assembler needs to generate code.
 */
void reset_address(void)
{
    tAsm.address = tAsm.start_address = tAsm.default_start_address;
}

/*
 ** Include a binary file
 */
void incbin(char *fname)
{
    FILE *input;
    char buf[256];
    int size;
    int i;

    input = fopen(fname, "rb");
    if (input == NULL) {
        sprintf(buf, "Error: Cannot open '%s' for input", fname);
        message(1, buf);
        return;
    }

    while ((size = fread(buf, 1, sizeof(buf), input)) != 0) {
        for (i = 0; i < size; i++) {
            emit_byte(buf[i]);
        }
    }

    fclose(input);
}

/*
 ** Do an assembler step
 */
void do_assembly(char *fname)
{
    FILE *input;
    char *p2;
    char *p3;
    char *pfname;
    int level;
    int avoid_level;
    int times;
    int base;
    int pline;
    int include;
    int align;

    input = fopen(fname, "r");
    if (input == NULL) {
        fprintf(stderr, "Error: cannot open '%s' for input\n", fname);
        tAsm.errors++;
        return;
    }

    pfname = tAsm.input_filename;
    pline = tAsm.line_number;
    tAsm.input_filename = fname;
    level = 0;
    avoid_level = -1;
    tAsm.global_label[0] = '\0';
    tAsm.line_number = 0;
    base = 0;
    while (fgets(tAsm.line, sizeof(tAsm.line), input)) {
        tAsm.line_number++;
        tAsm.p = tAsm.line;
        while (*tAsm.p) {
            if (*tAsm.p == '\'' && *(tAsm.p - 1) != '\\') {
                tAsm.p++;
                while (*tAsm.p && *tAsm.p != '\'' && *(tAsm.p - 1) != '\\')
                    tAsm.p++;
            } else if (*tAsm.p == '"' && *(tAsm.p - 1) != '\\') {
                tAsm.p++;
                while (*tAsm.p && *tAsm.p != '"' && *(tAsm.p - 1) != '\\')
                    tAsm.p++;
            } else if (*tAsm.p == ';') {
                while (*tAsm.p)
                    tAsm.p++;
                break;
            }
            *tAsm.p = toupper(*tAsm.p);
            tAsm.p++;
        }
        if (tAsm.p > tAsm.line && *(tAsm.p - 1) == '\n')
            tAsm.p--;
        *tAsm.p = '\0';

        base = tAsm.address;
        tAsm.g = tAsm.generated;
        include = 0;

        while (1) {
            tAsm.p = tAsm.line;
            separate();
            if (tAsm.part[0] == '\0' && (*tAsm.p == '\0' || *tAsm.p == ';'))    /* Empty line */
                break;
            if (tAsm.part[0] != '\0' && tAsm.part[strlen(tAsm.part) - 1] == ':') {    /* Label */
                tAsm.part[strlen(tAsm.part) - 1] = '\0';
                if (tAsm.part[0] == '.') {
                    strcpy(tAsm.name, tAsm.global_label);
                    strcat(tAsm.name, tAsm.part);
                } else {
                    strcpy(tAsm.name, tAsm.part);
                    strcpy(tAsm.global_label, tAsm.name);
                }
                separate();
                if (avoid_level == -1 || level < avoid_level) {
                    if (strcmp(tAsm.part, "EQU") == 0) {
                        p2 = match_expression(tAsm.p, &tAsm.instruction_value);
                        if (p2 == NULL) {
                            message(1, "bad expression");
                        } else {
                            if (tAsm.assembler_step == 1) {
                                if (find_label(tAsm.name)) {
                                    char m[18 + MAX_SIZE];

                                    sprintf(m, "Redefined label '%s'", tAsm.name);
                                    message(1, m);
                                } else {
                                    last_label = define_label(tAsm.name, tAsm.instruction_value);
                                }
                            } else {
                                last_label = find_label(tAsm.name);
                                if (last_label == NULL) {
                                    char m[33 + MAX_SIZE];

                                    sprintf(m, "Inconsistency, label '%s' not found", tAsm.name);
                                    message(1, m);
                                } else {
                                    if (last_label->value != tAsm.instruction_value) {
#ifdef DEBUG
/*                                        fprintf(stderr, "Woops: label '%s' changed value from %04x to %04x\n", last_label->name, last_label->value, tAsm.instruction_value);*/
#endif
                                        tAsm.change = 1;
                                    }
                                    last_label->value = tAsm.instruction_value;
                                }
                            }
                            check_end(p2);
                        }
                        break;
                    }
                    if (tAsm.first_time == 1) {
#ifdef DEBUG
                        /*                        fprintf(stderr, "First time '%s' at line %d\n", tAsm.line, tAsm.line_number);*/
#endif
                        tAsm.first_time = 0;
                        reset_address();
                    }
                    if (tAsm.assembler_step == 1) {
                        if (find_label(tAsm.name)) {
                            char m[18 + MAX_SIZE];

                            sprintf(m, "Redefined label '%s'", tAsm.name);
                            message(1, m);
                        } else {
                            last_label = define_label(tAsm.name, tAsm.address);
                        }
                    } else {
                        last_label = find_label(tAsm.name);
                        if (last_label == NULL) {
                            char m[33 + MAX_SIZE];

                            sprintf(m, "Inconsistency, label '%s' not found", tAsm.name);
                            message(1, m);
                        } else {
                            if (last_label->value != tAsm.address) {
#ifdef DEBUG
/*                                fprintf(stderr, "Woops: label '%s' changed value from %04x to %04x\n", last_label->name, last_label->value, tAsm.address);*/
#endif
                                tAsm.change = 1;
                            }
                            last_label->value = tAsm.address;
                        }

                    }
                }
            }
            if (strcmp(tAsm.part, "%IF") == 0) {
                level++;
                if (avoid_level != -1 && level >= avoid_level)
                    break;
                undefined = 0;
                tAsm.p = match_expression(tAsm.p, &tAsm.instruction_value);
                if (tAsm.p == NULL) {
                    message(1, "Bad expression");
                } else if (undefined) {
                    message(1, "Undefined labels");
                }
                if (tAsm.instruction_value != 0) {
                    ;
                } else {
                    avoid_level = level;
                }
                check_end(tAsm.p);
                break;
            }
            if (strcmp(tAsm.part, "%IFDEF") == 0) {
                level++;
                if (avoid_level != -1 && level >= avoid_level)
                    break;
                separate();
                if (find_label(tAsm.part) != NULL) {
                    ;
                } else {
                    avoid_level = level;
                }
                check_end(tAsm.p);
                break;
            }
            if (strcmp(tAsm.part, "%IFNDEF") == 0) {
                level++;
                if (avoid_level != -1 && level >= avoid_level)
                    break;
                separate();
                if (find_label(tAsm.part) == NULL) {
                    ;
                } else {
                    avoid_level = level;
                }
                check_end(tAsm.p);
                break;
            }
            if (strcmp(tAsm.part, "%ELSE") == 0) {
                if (avoid_level != -1 && level > avoid_level)
                    break;
                if (avoid_level == level) {
                    avoid_level = -1;
                } else if (avoid_level == -1) {
                    avoid_level = level;
                }
                check_end(tAsm.p);
                break;
            }
            if (strcmp(tAsm.part, "%ENDIF") == 0) {
                if (avoid_level == level)
                    avoid_level = -1;
                level--;
                check_end(tAsm.p);
                break;
            }
            if (avoid_level != -1 && level >= avoid_level) {
#ifdef DEBUG
                /*fprintf(stderr, "Avoiding '%s'\n", tAsm.line);*/
#endif
                break;
            }
            if (strcmp(tAsm.part, "USE16") == 0) {
                break;
            }
            if (strcmp(tAsm.part, "CPU") == 0) {
                tAsm.p = avoid_spaces(tAsm.p);
                if (memcmp(tAsm.p, "8086", 4) != 0)
                    message(1, "Unsupported processor requested");
                break;
            }
            if (strcmp(tAsm.part, "%INCLUDE") == 0) {
                separate();
                check_end(tAsm.p);
                if (tAsm.part[0] != '"' || tAsm.part[strlen(tAsm.part) - 1] != '"') {
                    message(1, "Missing quotes on %include");
                    break;
                }
                include = 1;
                break;
            }
            if (strcmp(tAsm.part, "INCBIN") == 0) {
                separate();
                check_end(tAsm.p);
                if (tAsm.part[0] != '"' || tAsm.part[strlen(tAsm.part) - 1] != '"') {
                    message(1, "Missing quotes on incbin");
                    break;
                }
                include = 2;
                break;
            }
            if (strcmp(tAsm.part, "ORG") == 0) {
                tAsm.p = avoid_spaces(tAsm.p);
                undefined = 0;
                p2 = match_expression(tAsm.p, &tAsm.instruction_value);
                if (p2 == NULL) {
                    message(1, "Bad expression");
                } else if (undefined) {
                    message(1, "Cannot use undefined labels");
                } else {
                    if (tAsm.first_time == 1) {
                        tAsm.first_time = 0;
                        tAsm.address = tAsm.instruction_value;
                        tAsm.start_address = tAsm.instruction_value;
                        base = tAsm.address;
                    } else {
                        if (tAsm.instruction_value < tAsm.address) {
                            message(1, "Backward address");
                        } else {
                            while (tAsm.address < tAsm.instruction_value)
                                emit_byte(0);

                        }
                    }
                    check_end(p2);
                }
                break;
            }
            if (strcmp(tAsm.part, "ALIGN") == 0) {
                tAsm.p = avoid_spaces(tAsm.p);
                undefined = 0;
                p2 = match_expression(tAsm.p, &tAsm.instruction_value);
                if (p2 == NULL) {
                    message(1, "Bad expression");
                } else if (undefined) {
                    message(1, "Cannot use undefined labels");
                } else {
                    align = tAsm.address / tAsm.instruction_value;
                    align = align * tAsm.instruction_value;
                    align = align + tAsm.instruction_value;
                    while (tAsm.address < align)
                        emit_byte(0x90);
                    check_end(p2);
                }
                break;
            }
            if (tAsm.first_time == 1) {
#ifdef DEBUG
                /*fprintf(stderr, "First time '%s' at line %d\n", tAsm.line, tAsm.line_number);*/
#endif
                tAsm.first_time = 0;
                reset_address();
            }
            times = 1;
            if (strcmp(tAsm.part, "TIMES") == 0) {
                undefined = 0;
                p2 = match_expression(tAsm.p, &tAsm.instruction_value);
                if (p2 == NULL) {
                    message(1, "bad expression");
                    break;
                }
                if (undefined) {
                    message(1, "non-constant expression");
                    break;
                }
                times = tAsm.instruction_value;
                tAsm.p = p2;
                separate();
            }
            base = tAsm.address;
            tAsm.g = tAsm.generated;
            p3 = tAsm.prev_p;
            while (times) {
                tAsm.p = p3;
                separate();
                process_instruction();
                times--;
            }
            break;
        }
        if (tAsm.assembler_step == 2 && tAsm.listing != NULL) {
            if (tAsm.first_time)
                fprintf(tAsm.listing, "      ");
            else
                fprintf(tAsm.listing, "%04X  ", base);
            tAsm.p = tAsm.generated;
            while (tAsm.p < tAsm.g) {
                fprintf(tAsm.listing, "%02X", *tAsm.p++ & 255);
            }
            while (tAsm.p < tAsm.generated + sizeof(tAsm.generated)) {
                fprintf(tAsm.listing, "  ");
                tAsm.p++;
            }
            fprintf(tAsm.listing, "  %05d %s\n", tAsm.line_number, tAsm.line);
        }
        if (include == 1) {
            tAsm.part[strlen(tAsm.part) - 1] = '\0';
            do_assembly(tAsm.part + 1);
        }
        if (include == 2) {
            tAsm.part[strlen(tAsm.part) - 1] = '\0';
            incbin(tAsm.part + 1);
        }
    }
    fclose(input);
    tAsm.line_number = pline;
    tAsm.input_filename = pfname;
}

/*
 ** Main program
 */
int main(int argc, char *argv[])
{
    int c;
    int d;
    char *p;
    char *ifname;

    /*
     ** If ran without arguments then show usage
     */
    if (argc == 1) {
        fprintf(stderr, "Typical usage:\n");
        fprintf(stderr, "tinyasm -f bin input.asm -o input.bin\n");
        exit(1);
    }

    /*
     ** Start to collect arguments
     */
    ifname = NULL;
    tAsm.output_filename = NULL;
    tAsm.listing_filename = NULL;
    tAsm.default_start_address = 0;
    c = 1;
    while (c < argc) {
        if (argv[c][0] == '-') {    /* All arguments start with dash */
            d = tolower(argv[c][1]);
            if (d == 'f') { /* Format */
                c++;
                if (c >= argc) {
                    fprintf(stderr, "Error: no argument for -f\n");
                    exit(1);
                } else {
                    to_lowercase(argv[c]);
                    if (strcmp(argv[c], "bin") == 0) {
                        tAsm.default_start_address = 0;
                    } else if (strcmp(argv[c], "com") == 0) {
                        tAsm.default_start_address = 0x0100;
                    } else {
                        fprintf(stderr, "Error: only 'bin', 'com' supported for -f (it is '%s')\n", argv[c]);
                        exit(1);
                    }
                    c++;
                }
            } else if (d == 'o') {  /* Object file name */
                c++;
                if (c >= argc) {
                    fprintf(stderr, "Error: no argument for -o\n");
                    exit(1);
                } else if (tAsm.output_filename != NULL) {
                    fprintf(stderr, "Error: already a -o argument is present\n");
                    exit(1);
                } else {
                    tAsm.output_filename = argv[c];
                    c++;
                }
            } else if (d == 'l') {  /* Listing file name */
                c++;
                if (c >= argc) {
                    fprintf(stderr, "Error: no argument for -l\n");
                    exit(1);
                } else if (tAsm.listing_filename != NULL) {
                    fprintf(stderr, "Error: already a -l argument is present\n");
                    exit(1);
                } else {
                    tAsm.listing_filename = argv[c];
                    c++;
                }
            } else if (d == 'd') {  /* Define label */
                p = argv[c] + 2;
                while (*p && *p != '=') {
                    *p = toupper(*p);
                    p++;
                }
                if (*p == '=') {
                    *p++ = 0;
                    undefined = 0;
                    p = match_expression(p, &tAsm.instruction_value);
                    if (p == NULL) {
                        fprintf(stderr, "Error: wrong label definition\n");
                        exit(1);
                    } else if (undefined) {
                        fprintf(stderr, "Error: non-constant label definition\n");
                        exit(1);
                    } else {
                        define_label(argv[c] + 2, tAsm.instruction_value);
                    }
                }
                c++;
            } else {
                fprintf(stderr, "Error: unknown argument %s\n", argv[c]);
                exit(1);
            }
        } else {
            if (ifname != NULL) {
                fprintf(stderr, "Error: more than one input file name: %s\n", argv[c]);
                exit(1);
            } else {
                ifname = argv[c];
            }
            c++;
        }
    }

    if (ifname == NULL) {
        fprintf(stderr, "No input filename provided\n");
        exit(1);
    }

    /*
     ** Do first step of assembly
     */
    tAsm.assembler_step = 1;
    tAsm.first_time = 1;
    do_assembly(ifname);
    if (!tAsm.errors) {

        /*
         ** Do second step of assembly and generate final output
         */
        if (tAsm.output_filename == NULL) {
            fprintf(stderr, "No output filename provided\n");
            exit(1);
        }
        tAsm.change_number = 0;
        do {
            tAsm.change = 0;
            if (tAsm.listing_filename != NULL) {
                tAsm.listing = fopen(tAsm.listing_filename, "w");
                if (tAsm.listing == NULL) {
                    fprintf(stderr, "Error: couldn't open '%s' as listing file\n", tAsm.output_filename);
                    exit(1);
                }
            }
            tAsm.output = fopen(tAsm.output_filename, "wb");
            if (tAsm.output == NULL) {
                fprintf(stderr, "Error: couldn't open '%s' as output file\n", tAsm.output_filename);
                exit(1);
            }
            tAsm.assembler_step = 2;
            tAsm.first_time = 1;
            do_assembly(ifname);

            if (tAsm.listing != NULL && tAsm.change == 0) {
                fprintf(tAsm.listing, "\n%05d ERRORS FOUND\n", tAsm.errors);
                fprintf(tAsm.listing, "%05d WARNINGS FOUND\n\n", tAsm.warnings);
                fprintf(tAsm.listing, "%05d PROGRAM BYTES\n\n", tAsm.bytes);
                if (label_list != NULL) {
                    fprintf(tAsm.listing, "%-20s VALUE/ADDRESS\n\n", "LABEL");
                    sort_labels(label_list);
                }
            }
            fclose(tAsm.output);
            if (tAsm.listing_filename != NULL)
                fclose(tAsm.listing);
            if (tAsm.change) {
                tAsm.change_number++;
                if (tAsm.change_number == 5) {
                    fprintf(stderr, "Aborted: Couldn't stabilize moving label\n");
                    tAsm.errors++;
                }
            }
            if (tAsm.errors) {
                remove(tAsm.output_filename);
                if (tAsm.listing_filename != NULL)
                    remove(tAsm.listing_filename);
                exit(1);
            }
        } while (tAsm.change) ;

        exit(0);
    }

    exit(1);
}
/* vim: set tabstop=4 softtabstop=4 expandtab shiftwidth=4 autoindent: */
