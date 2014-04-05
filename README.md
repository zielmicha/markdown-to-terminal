Libsoldout
==========

Overview:

1. Introduction
2. Usage
3. Internals



Introduction
------------

For some projects of mine, I wanted a lightweight C library that can parse
John Gruber's [markdown](http://daringfireball.net/projects/markdown/)
format into whatever I want, and that is easily extendable.

I found two existing C implementations for markdown, but they weren't 
quite the "fit" that I wanted:

  * [Discount](http://www.pell.portland.or.us/~orc/Code/markdown/), 
    which seemed a little bit too integrated and focused on HTML output 
    for my taste, and 
  * [PEG-markdown](http://github.com/jgm/peg-markdown/tree/master), which 
    seemed to have a lot of dependencies and stuff. 
  
So I wrote my own.

I like to keep things simple, so I wrote a function that performs *only*
markdown parsing—no file reading or writing, no (X)HTML considerations,
etc. 

Output is performed by a set of dedicated callback functions (here called a 
"renderer"). Some example renderers are provided, but you are free to use 
your own to output in any format you like.

This callback mechanism make libsoldout so flexible that it does not need
any flag or external information besides input text and renderer to
operate.



Usage
-----

### Library function call

The only exported function in libsoldout is `markdown()`:

```c
    void markdown(struct buf *ob, struct buf *ib, const struct mkd_renderer *rndr);
```

  * `ob` is the output buffer, where the renderer will append data
  * `ib` is the input buffer, where the markdown text should be stored prior
    to the `markdown()` call
  * `rndr` is a pointer to the renderer structure

Read on to learn how to use these structures.


### Buffers: `struct buf`

I use `struct buf` extensively in input and output buffers. The initial
idea was constructing a Pascal-string like structure, to be able to store
both text and binary data. Hence the members `data` (a `char` pointer to the
buffer data) and `size` (containing the data length).

When using a `struct buf` as an output buffer, it is useful to pre-allocate
the memory area before filling it, so I added an `asize` member containing
the allocated size of the memory pointed by `data`.

When accumulating data in a growing memory area, there is a tradeoff
between memory usage and speed: the more bytes are added each time, the
less `realloc()` is called, which (potentially) means less `memcpy()` to a new
zone, so a faster code, but more memory being allocated for nothing. To
set the tradeoff on a case-by-case basis, there is a `unit` member in the
structure: when more memory is needed, `asize` is augmented by a multiple
of `unit`. So the larger `unit`, the more memory is allocated at once, the
`reallioc()` is called.

To further improve code efficiency by removing unneeded memcpy, I added a
reference count to the structure: the `ref` member.

Buffers are created using `bufnew()`. Its only argument is the value for
`unit`. `bufrelease()` decreases the reference count of a buffer, and frees
it when this count is zero. `bufset()` is used to set a `struct buf`
pointer to point to the given buffer, increasing reference count and
dealing with special cases like volatile buffers.

Usually data from `struct buf` are read through direct access of its
members `data` and `size`. One interesting trick is calling `printf` on a 
buffer (or any kind of non-zero-terminated string), provided it doesn't 
contain zeroes, using `%.*s`. For example:

```c
	printf("Buffer string: \"%.*s\"\n", (int)buf->size, buf->data);
```

In case you really need a zero-terminated string, you can call
`bufnullterm()`, which appends a zero character without changing `size`.
This allows the buffer to remain virtually the same (and will no longer be
zero-terminated after the following data append) but `data` can be used as
a regular C string.

The most common functions to append data into buffers are:

  * `bufprintf()` which behaves like any `*printf` function
  * `bufput()` which is similar to `memcpy()`
  * `bufputs()` which appends a zero-terminated string to a buffer
  * `BUFPUTSL()` which is a macro to replace `bufputs()` when using string
    literals (when data size is known at compile-time, this saves a call 
    to `strlen()`)
  * `bufputc()` for single-character appends

Modification of existing data in a buffer is also performed through direct
access of structure members.

This covers the basics to handle my `struct buf`. But you may still learn 
some interesting stuff from the header.


### Renderer: `struct mkd_renderer`

Libsoldout only performs the parsing of markdown input, the construction of
the output is left to a *renderer*, which is a set of callback functions
called when markdown elements are encountered. Pointers to these functions
are gathered into a `struct mkd_renderer` along with some renderer-related
data. I think the struct declaration is pretty obvious:

```c
	struct mkd_renderer {
		/* document level callbacks */
		void (*prolog)(struct buf *ob, void *opaque);
		void (*epilog)(struct buf *ob, void *opaque);
	
		/* block level callbacks - NULL skips the block */
		void (*blockcode)(struct buf *ob, struct buf *text, void *opaque);
		void (*blockquote)(struct buf *ob, struct buf *text, void *opaque);
		void (*blockhtml)(struct buf *ob, struct buf *text, void *opaque);
		void (*header)(struct buf *ob, struct buf *text,
							int level, void *opaque);
		void (*hrule)(struct buf *ob, void *opaque);
		void (*list)(struct buf *ob, struct buf *text, int flags, void *opaque);
		void (*listitem)(struct buf *ob, struct buf *text,
							int flags, void *opaque);
		void (*paragraph)(struct buf *ob, struct buf *text, void *opaque);
		void (*table)(struct buf *ob, struct buf *head_row, struct buf *rows,
								void *opaque);
		void (*table_cell)(struct buf *ob, struct buf *text, int flags,
								void *opaque);
		void (*table_row)(struct buf *ob, struct buf *cells, int flags,
								void *opaque);
	
		/* span level callbacks - NULL or return 0 prints the span verbatim */
		int (*autolink)(struct buf *ob, struct buf *link,
						enum mkd_autolink type, void *opaque);
		int (*codespan)(struct buf *ob, struct buf *text, void *opaque);
		int (*double_emphasis)(struct buf *ob, struct buf *text,
							char c, void *opaque);
		int (*emphasis)(struct buf *ob, struct buf *text, char c,void*opaque);
		int (*image)(struct buf *ob, struct buf *link, struct buf *title,
							struct buf *alt, void *opaque);
		int (*linebreak)(struct buf *ob, void *opaque);
		int (*link)(struct buf *ob, struct buf *link, struct buf *title,
						struct buf *content, void *opaque);
		int (*raw_html_tag)(struct buf *ob, struct buf *tag, void *opaque);
		int (*triple_emphasis)(struct buf *ob, struct buf *text,
							char c, void *opaque);
	
		/* low level callbacks - NULL copies input directly into the output */
		void (*entity)(struct buf *ob, struct buf *entity, void *opaque);
		void (*normal_text)(struct buf *ob, struct buf *text, void *opaque);
	
		/* renderer data */
		int max_work_stack; /* prevent arbitrary deep recursion */
		const char *emph_chars; /* chars that trigger emphasis rendering */
		void *opaque; /* opaque data send to every rendering callback */
	};
```

The first argument of a renderer function is always the output buffer,
where the function is will write its output. It's not necessarily
related to the output buffer given to `markdown()`, because a temporary 
buffer is sometimes needed for rendering.

The last argument of a renderer function is always an opaque pointer, which
is equal to the `opaque` member of `struct mkd_renderer`. The name
"opaque" might not be well-chosen, but it means a pointer *opaque for the
parser, **not** for the renderer*. This means the parser blindly passes around
the pointer containining data you know about, in case you need to
store an internal state or whatever. (I haven't found anything to put in
this pointer in my example renderers, so it is set to `NULL` in the structure
and never used by the callbacks.)

`emph_chars` is a zero-terminated string which contains the set of
characters that trigger emphasis. In regular markdown, emphasis is only
triggered by `_` and `*`, but in some extensions it might be useful to
add other characters to this list. For example, in my extension to handle
`<ins>` and `<del>` spans, delimited respectively by `++` and `--`, I have
added `+` and `-` to `emph_chars`. The character that triggered the
emphasis is then passed to `emphasis`, `double_emphasis` and `triple_emphasis`, 
through the parameter `c`.

Function pointers in `struct mkd_renderer` can be `NULL`, but it has a
different meaning, depending on whether the callback is block-level or 
span-level. A null block-level callback will make the corresponding block 
disappear from the output, as if the callback was an empty function. 
A null span-level callback will cause the corresponding element to be treated 
as normal characters, and copied verbatim to the output.

So for example, to disable link and images (e.g. because you consider them
as dangerous), just put a null pointer in `rndr.link` and `rndr.image` and
the bracketed stuff will be present as-is in the output. While a null
pointer in `header` will remove all header-looking blocks. If you want an
otherwise standard markdown-to-XHTML conversion, you can take the example
`mkd_xhtml` struct, copy it into your own `struct mkd_renderer` and then
assign `NULL` to `link` and `image` members.

Moreover, span-level callbacks return an integer, which indicates whether 
the renderer will render the item (non-zero return value) or whether it
should be copied verbatim (zero return value). This allows you to only
accept some specific inputs. For example, my extension for `<ins>` and
`<del>` spans asks *exactly* two `-` or `+` delimiters; so, when `emphasis`
and `triple_emphasis` are called with `-` or `+`, they return `0`.

Special care should be taken when writing `autolink`, `link` and `image`
callbacks: the arguments `link`, `title`, and `alt` are unsanitized data, 
taken directly from the input file. It is up to the renderer to escape
whatever needs escaping to prevent bad things from happening. To help you
writing renderers, the function `lus_attr_escape()` escapes all problematic
characters in (X)HTML: `<`, `>`, `&` and `"`.

The `normal_text` callback should also perform whatever escape is needed to
have the output looking like the input data.


### libsoldout extension: PHP-Markdown-like tables

Tables are one of the few extensions that are quite difficult and/or hacky
to implement using vanilla Markdown parser and a renderer. Thus a support
has been introduced into the parser, using dedicated callbacks:

  * `table_cell`, which is called with the span-level contents of the cell;
  * `table_row`, which is called with data returned by `table_cell`;
  * `table`, which called with data returned by `table_row`.

The input format to describe tables is taken from PHP-Markdown, and looks
like this:

```markdown
	header 1    | header 2      | header 3      | header 4
	------------|:-------------:|--------------:|:--------------
	first line  |   centered    | right-aligned | left-aligned
	second line |   centered    |:   centered  :| left-aligned
	third line  |: left-aglined | right-aligned | right-aligned :
	column-separator | don't need | to be | aligned in the source
	| extra speratators | are allowed | at both ends | of the line |
	| correct number of cell per row is not enforced |
	| pipe characters can be embedded in cell text by escaping it: \| |
```

Each row of the input text is a single row in the output, except the header
rule, which is purely syntactic.

Each cell in a row is delimited by the pipe (`|`) character. Optionally, a
pipe character can also be present at the beginning and/or at the end of
the line. Column separators don't have to be aligned (although it does make the 
input more readable).

There is no check of "squareness" of the table: `table_cell` is called once
for each cell provided in the input, which can be a number of times
different from one row to the other. If the output *has* to respect a given
number of cells per row, it's up to the renderer to enforce it (using state
transmitted through the `opaque` pointer).

The header rule is a line containing only horizontal blanks (space and
tab), hyphens (`-`), colons (`:`), and separator. Moreover, it *must* be the
second line of the table. When a header rule is detected, the first
line of the table is considered as a header, and passed as the `head_row`
argument to `table` callback. Additionally, `table_row` and `table_cell` are
called for that specific row with `MKD_CELL_HEAD` flag.

Alignment is defined on a per-cell basis, and specified by a colon (`:`) at
the very beginning of the input span (i.e. directly after the `|`
separator, or as the first character on the line) and/or at the very end of
it (i.e.  directly before the separator, or as the last character on the
line). A cell with such a leading colon only is left-aligned
(`MKD_CELL_ALIGN_LEFT`), one with a trailing colon only is right-aligned
(`MKD_CELL_ALIGN_RIGHT`), and one with both is centered
(`MKD_CELL_ALIGN_CENTER`).

A column-wise default alignment can be specified with the same syntax on
the header rule.


### Renderer examples

While libsoldout is designed only to parse of markdown files (and allow you 
to provide the renderer callbacks), a few renderers have been included. 
They exist to illustrate how to write a set of renderer functions, and
to allow anybody who does not need special extensions to use libsoldout
without hassle.

All examples provided here come in two flavors: 

 * `_html`  producing HTML code (self-closing tags are rendered like `<hr>`)
 * `_xhtml` producing XHTML code (self-closing tags are rendered like `<hr />`)

#### Standard markdown renderer

`mkd_html` and `mkd_xhtml` implement standard Markdown to (X)HTML
translation without any extension.

#### Discount-ish renderer

`discount_html` and `discount_xhtml` implement on top of the standard
markdown *some* of the extensions found in Discount.

Actually, all Discount extensions that are not provided here cannot be
easily implemented in libsoldout without touching the parsing code;
hence, they do not belong strictly to the renderer realm. However some
(perhaps all?—I'm uncertain about tables) extensions can be implemented fairly
easily with libsoldout by using both a dedicated renderer and some
preprocessing (to make the extension look like something closer to the
original markdown syntax).

Here are all the extensions included in the renderers:

 - image size specification by appending ` =(width)x(height)` to the link
 - pseudo-protocols in links:
   * `abbr`:«description» for `<abbr title="`«description»`">...</abbr>`
   * `class`:«name» for `<span class="`«name»`">...</span>`
   * `id`:«name» for `<a id="`«name»`>...</a>`
   * `raw`:«text» for verbatim unprocessed «text» inclusion
 - `class` blocks: blockquotes beginning with `%_class_%` will be rendered as a
   `div` of the given class(es).

#### Natasha's own extensions

`nat_html` and `nat_xhtml` implement (on top of Discount extensions) some
things that I need to losslessly convert my existing HTML into extended
markdown.

Here is a list of these extensions:

  * `id` attribute for headers, using the syntax `id#«Header text»`
  * `class` attribute for paragraphs, by putting class name(s) between
    parentheses at the very beginning of the paragraph
  * `<ins>` and `<del>` spans (using `++` and `--` respectively);
    there are necessarily emphasis-like restrictions (i.e. no whitespace 
    allowed after an opening delimiter, or before a closing delimiter)
  * plain `<span>` without attributes, using the (emphasis-like) delimiter `|`

Here's an example using of all of them:

```markdown
	###atx_id#ID was chosen to look nice in atx-style headers ###
	
	setext_id#Though it will also work in setext-style headers
	----------------------------------------------------------
	
	Here is a paragraph with --deleted-- and ++inserted++ text.
	
	I use CSS rules to render poetry and other verses, using a plain
	`<span>` for each verse, and enclosing each group of verses in
	a `<p class="verse">`. Here is how it would look like:
	
	(verse)|And on the pedestal these words appear:|
	|"My name is Ozymandias, king of kings:|
	|Look on my works, ye Mighty, and despair!"|
```



Internals
---------

Here I explain the structure of `markdown.c`, and how this parser works. I
use a logical order, which is roughly chronological, which means going
roughly from the bottom of the file to the top.


### `markdown()`

The markdown function is divded into four parts: setup of the `struct
render`, first pass on the input, actual parsing, and clean-up.

#### render structure

A `struct render` is passed around most of the functions, and it contains
every information specific about the render.

`make` is a copy of the `struct mkd_renderer` given to `markdown()`. The
rendering callbacks are actually called from there.

`refs` is a dynamic sorted array of link references (`struct link_ref`). It
is filled from the input file during the first pass. A link reference is a
structure of three buffers, `id`, `link` and `title`, whose functions are
straightforward.

`work` is a dynamic array of working buffers. Short-lived working buffers are
needed throughout the parser, and doing a lot of `malloc()` and `free()` is
quite inefficient. Instead, when a working buffer is allocated, it is kept
in this array to be rused next time a working buffer is needed.

`active_char` is a C array of function pointeurs, used for span-level
parsing: a null pointer is affecter to all inactive characters, and a
specialized callback is stored for active characters. This initialization
is the bulk of the first part, because characters should only be marked
active when the rendering callback pointer is non-null.

#### Input: First pass

During the first pass on the input, newlines are normalized and reference
lines taken out of the input, and stored into `rndr.refs`.

It makes use of the helper function `is_ref()`, which parses the given
line, checking whether it matches the reference syntax. Offsets of the
reference components are kept while progressing in the line, and on the
first syntax error `0` is returned, and the line is considered as an 
input line.

When all the tests are passed, a new `struct link_ref` is created and
sorted into `rndr.refs`.

#### Input: Second pass

`markdown()` does not do much here, the result of the first pass is fed to
`parse_block()` which fills the output buffer `ob`.

#### Clean-up

References allocated during the first pass, and working buffers allocated
during the second pass are freed there, before returning.


### Block-level parsing

The core of block-level parsing is the function `parse_block()`, which
runs over the whole input (on the first call, the input is the output on
the first pass, but `parse_block()` can be called recursively for blocks
inside blocks, e.g. for blockquotes).

The kind of block at the beginning of the input is determined using the
`prefix_*` functions, then the correct `parse_<block>` function is called
for the current block. All specialized `parse_<block>` functions returns a
`size_t` which is the size of the current block. This lets `parse_block()`
know where to start looking for the following block.

Some blocks are easy to handle, for example blocks of code: the
`parse_blockcode()` functions only scans the input, accumulating lines in a
working buffer after stripping the blockcode prefix, and stopping at the
first non-empty non-blockcode-prefixed line. It then calls the rendering
function for block codes and returns.

Other blocks are more complicated, like paragraphs who can actually be
setxt-style headers, or list items, which require a special subparse to
follow Markdown rules where sublist creation is more laxist than list
creation.

Most block functions call `parse_inline()` for span-level parsing, before
handing the result to the block renderer callback.

#### HTML block parsing

The `parse_htmlblock()` function is of interest; according to the Markdown
documentations, HTML blocks must be delimited by unindented block-level tags
(with the opening tag being preceeded by a blank line, and the closing tag
being followed by a blank line).

When examining the reference implementation (`Markdown.pl`), it appeared
that when this doesn't find a match, a more lax syntax is tried, where
the closing tag can be indented; it merely has to be at the end of line and
followed by a blank line.

But when looking at the test suite, it appeared that a single line
`<div>foo</div>` surrounded by blank lines *should* be recorgnized as a
block, regardless of the "matching" unindented closing tag at the end of
the document. This meant that only the laxist approach should be used.

This is why the first pass is commented with a `#if 0`. If you prefer strict
HTML block parsing (as described on the webpage), you should instead comment
the second pass. Keeping both first and second passes yields the same
behaviour as `Markdown.pl` v1.0.1.

I have to admit, I don't really care about these differences. I do not personally
intend to use any inline HTML: I can choose to parse unsafe input (in which
case inline HTML is too dangerous), or parse my own input. But I use Markdown 
when I'm not confident in my HTML correctness, so it would be useless to 
include HTML in my input. However, I realize this feature does matter for some 
people, so I welcome any patche or suggestion you may have to "fix" this 
behaviour.


### Span-level parsing

The core of span-level parsing is the function `parse_inline()`, which is
pretty different from `parse_block()`. It is based around the
`active_char[]` vector table in the render structure.

The main loop is composed of two parts: first the next active character is
looked for. The string of inactive characters is directly handed over to
`normal_text` rendering callback.

When a character is active, its corresponding entry in the `active_char[]`
is a pointer to one of the `char_*` functions. Most of these functions do
pretty straightforward work in performing their role.

The most complicated of these functions is `char_link`, which responds to
`'['`. This is because of the many possibilities offered by markdown to use
this character: it can either be a part of a link or an image, and then it
can be inline or reference style or a shortcut reference style.

Emphasis is another interesting piece of code. When encountering an emphasis 
character, it first looks whether it is single or double or triple emphasis, 
and then continues forward in search of a match.


### Proof that recursion depth is bound by `max_work_stack`

The core of the code here is that when entering the functions
`parse_inline()` and `parse_block()`, if the current size of the working
buffer stack (`rndr->work`) is above `max_work_stack`, the parsing is
short-circuited and the input is appended as-is.

Let's prove now that this actually works:

**Step 1:** There is no function calling itself directly in `markdown.c`.
This is quite easy to check, though a bit tedious. This proves that a stack
overflow involves a recursion cycle of a least two functions.

**Step 2:** Most functions in `markdown.c` are declared by their definition, 
which means that these functions can only call functions that precede them in 
the source file. This provides a strict hierarchy, which prevents any 
multiple-function recursion cycle. Thus, only exceptions to the hierarchy are 
left to check.

**Step 3:** There are only 3 functions that break the above-mentioned
hierarchy:

  * `markdown()`, which is declared through the inclusion of `markdown.h`
    at the very beginning of `markdown.c`. However an easy text search shows
    that it's actually never called here, which obviously prevents it from
    being part of a recursion cycle.
  * `parse_block()`, which is declared at the beginning of the block-level
    section, but defined at the end.
  * `parse_inline()`, which uses functions pointer to dispatch active
    character handling towards `char_*` functions below.

So far, we've proved that any recursion cycle *always* involves
`parse_block()` or `parse_inline()`. Therefore, a mere depth-indicator check 
is enough to prevent recursion cycles in these functions.

**Step 4**: `rndr->work.size` is a good depth-indicator, because all calls
to `parse_block()` or `parse_inline()` happen after at least one working
buffer allocation. Again, checking this is a bit tedious:

  * `parse_block()` is called in `markdown()` (which is irrelevant), and in
    `parse_blockquote()` and `parse_listitem()`, which respectively allocate
    one and two working buffers at the very beginning of the function.
  * `parse_inline()` is called in `parse_emph1()`, `parse_emph2()`,
    `parse_emph3()`, `char_link()`, `parse_paragraph()` (twice). Each time,
    it's called immediately after allocating a new working buffer, and in
    `parse_listitem()` (which allocates two working buffers) at the very
    beginning of the functions.

Therefore, `rndr->work.size` will always increase between calls of
`parse_block()` or `parse_inline()`. This proves that putting an
upper bound on `rndr->work.size` prevents arbitrarily deep recursions
(and therefore, stack overflows) when the upper bound is well chosen.


### Utility functions

Throughout the parsing the need of a working buffer frequently arise. A
naive approach is to allocate a working buffer each time one is needed, and
release it afterwards. However it leads to a lot of allocations,
deallocations and reallocations (when the buffer grows), which costs a lot
of time.

So I added a `work` dynamic array pointer, which a special meaning to the
`size` and `asize` members. In this array, the `size` first members are
active working buffers that are still in use, and the remaining members up
to `asize` are allocated but unused working buffers.

When a function needs a working buffer, it first compares `size` to `asize`.
When they are equal, it means there is no working buffer is available, and a
new one is created and appended (`push`ed) to the array. Otherwise it
increases `size` and takes the already-allocated buffer as its working
buffer, resetting its size.

When the working buffer is no longer needed, the `size` of the array is
simply decreased. That way, the buffer is still allocated, but ready to be 
taken by the next function in need.

When the parsing is complete, every working buffer should be marked as ready 
for reuse, hence the assertion of `size` being zero in `markdown()`. The
buffers in the array are finally freed.
