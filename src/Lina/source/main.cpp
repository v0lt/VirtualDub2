// Lina - HTML compiler for VirtualDub help system
//
// Copyright (C) 1998-2003 Avery Lee
// Copyright (C) 2024-2026 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include <sys/stat.h>
#include <direct.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <string>
#include <list>
#include <map>
#include <set>
#include <vector>
#include <utility>
#include <ranges>
#include "document.h"
#include "parser.h"

///////////////////////////////////////////////////////////////////////////

std::list<std::pair<std::string, bool> > g_truncateURLs;
std::list<std::string> g_htmlHelpFiles;

///////////////////////////////////////////////////////////////////////////

struct Context {
	TreeDocument *mpDocument;
	std::list<const TreeNode *> stack;
	std::list<const TreeNode *> invocation_stack;
	std::list<TreeNode *> construction_stack;
	int pre_count;
	int cdata_count;
	bool eat_next_space;
	bool holding_space;

	Context() : pre_count(0), cdata_count(0), eat_next_space(true), holding_space(false) {}

	const TreeNode *find_tag(std::string name) {
		const TreeNode *t = NULL;

		for(const auto item : invocation_stack | std::views::reverse) {
			t = item->Child(name);
			if (t) {
				break;
			}
			if (!name.empty() && name[0] == '/') {
				break;
			}
		}

		return t;
	}
};

void output_tag(Context& ctx, std::string *out, const TreeNode& tag);
void output_tag_contents(Context& ctx, std::string *out, const TreeNode& tag);

//////////////////////////////////////////////////////////////

std::string g_outputDir;

typedef std::map<std::string, std::string> tFileCopies;
tFileCopies g_fileCopies;

//////////////////////////////////////////////////////////////

void error(const Context& ctx, const char *format, ...)
{
	va_list val;

	printf("%s(%d): Error! ", ctx.stack.back()->mpLocation->mName.c_str(), ctx.stack.back()->mLineno);

	va_start(val, format);
	vprintf(format, val);
	va_end(val);
	putchar('\n');

	int indent = 3;
	for (const auto item : ctx.stack | std::views::reverse) {
		const TreeNode& tag = *item;
		printf("%*c%s(%d): while processing tag <%s>\n", indent, ' ', tag.mpLocation->mName.c_str(), tag.mLineno, tag.mName.c_str());
		indent += 3;
	}

	indent = 3;
	for (const auto item : ctx.invocation_stack | std::views::reverse) {
		const TreeNode& tag = *item;
		printf("%*c%s(%d): while invoked from tag <%s> (%zu children)\n", indent, ' ', tag.mpLocation->mName.c_str(), tag.mLineno, tag.mName.c_str(), tag.mChildren.size());
		indent += 3;
	}

	exit(10);
}

//////////////////////////////////////////////////////////////

std::string create_output_filename(const std::string& name) {
	std::string filename(g_outputDir);

	if (!filename.empty()) {
		char c = filename[filename.size()-1];
		if (c != '/' && c != '\\')
			filename += '/';
	}

	filename += name;

	return filename;
}

void construct_path(const std::string& dstfile) {
	int idx = -1;

	for(;;) {
		int pos = dstfile.find_first_of("\\/", idx+1);

		if (pos == std::string::npos)
			break;

		std::string partialpath(dstfile.substr(0, pos));
		struct _stat buffer;

		if (-1 == _stat(partialpath.c_str(), &buffer)) {
			printf("creating: %s\n", partialpath.c_str());
			_mkdir(partialpath.c_str());
		}

		idx = pos;
	}
}

void copy_file(const std::string& dstfile, const std::string& srcfile) {
	printf("copying: %s -> %s\n", srcfile.c_str(), dstfile.c_str());

	FILE *fs = nullptr;
	errno_t err = fopen_s(&fs, srcfile.c_str(), "rb");
	if (err) {
		error("couldn't open source file \"%s\"", srcfile.c_str());
	}

	std::string filename(g_outputDir);

	if (!filename.empty()) {
		char c = filename[filename.size()-1];
		if (c != '/' && c != '\\')
			filename += '/';
	}

	filename += dstfile;

	construct_path(filename);

	FILE *fd = nullptr;
	err = fopen_s(&fd, filename.c_str(), "wb");
	if (err) {
		error("couldn't create \"%s\"", filename.c_str());
	}

	fseek(fs, 0, SEEK_END);
	std::vector<char> data(ftell(fs));
	fseek(fs, 0, SEEK_SET);
	if (1 != fread(&data.front(), data.size(), 1, fs))
		error("couldn't read from \"%s\"", srcfile.c_str());
	fclose(fs);

	if (1 != fwrite(&data.front(), data.size(), 1, fd) || fclose(fd))
		error("couldn't write to \"%s\"", dstfile.c_str());
}

bool is_true(const std::string& name) {
	return name.empty() || name[0]=='y' || name[0]=='Y';
}

void dump_parse_tree(const TreeNode& tag, int indent = 0) {
	if (tag.mbIsText) {
	} else if (tag.mChildren.empty()) {
		printf("%*c<%s/>\n", indent, ' ', tag.mName.c_str());
	} else {
		printf("%*c<%s>\n", indent, ' ', tag.mName.c_str());

		for(const auto& child : tag.mChildren) {
			dump_parse_tree(*child, indent+3);
		}

		printf("%*c</%s>\n", indent, ' ', tag.mName.c_str());
	}
}

////////////////////////////////////////////////////////////////////////////////

void output_tag_attributes(std::string& out, const TreeNode& tag)
{
	bool is_anchor = (tag.mName == "a");
	
	for(const auto& att : tag.mAttribs) {
		out += ' ';
		out += att.mName;

		if (!att.mbNoValue) {
			auto its(att.mValue.cbegin()), itsEnd(att.mValue.cend());
			for (; its != itsEnd; ++its) {
				if (!issafevaluechar(*its)) {
					break;
				}
			}

			std::string value(att.mValue);

			if (is_anchor && att.mName == "href") {
				for(const auto& entry : g_truncateURLs) {
					if (value.length() >= entry.first.length() && !value.compare(0, entry.first.length(), entry.first)) {
						if (entry.second) {
							int l = value.length();

							while(l>0) {
								char c = value[--l];

								if (c == '/' || c == ':')
									break;
								if (c == '.') {
									if (value.substr(l+1, std::string::npos) == "html")
										value.erase(l, std::string::npos);
									break;
								}
							}
							printf("truncated link: %s\n", value.c_str());
						}
						break;
					}
				}
			}

			if (att.mValue.empty() || its!=itsEnd) {
				out += "=\"";
				out += value;
				out += '"';
			} else {
				out += '=';
				out += value;
			}
		}
	}
}

void output_tag_contents(Context& ctx, std::string *out, const TreeNode& tag) {
	static int recursion_depth = 0;

	++recursion_depth;

	if (recursion_depth > 64) {
		error(ctx, "recursion exceeded limits");
	}

	for(const auto& child : tag.mChildren) {
		output_tag(ctx, out, *child);
	}

	--recursion_depth;
}

void output_standard_tag(Context& ctx, std::string *out, const TreeNode& tag) {
	if (!tag.mbIsText && tag.mName == "pre")
		++ctx.pre_count;

	if (out && (tag.mbIsControl || !tag.mbIsText)) {
		if (ctx.holding_space && ctx.cdata_count) {
			if (!ctx.eat_next_space) {
				*out += ' ';
			}
			ctx.eat_next_space = false;
			ctx.holding_space = false;
		}
	}

	if (!ctx.construction_stack.empty()) {
		TreeNode *new_tag = ctx.mpDocument->AllocNode();

		new_tag->mpLocation		= tag.mpLocation;
		new_tag->mLineno		= tag.mLineno;
		new_tag->mName			= tag.mName;
		new_tag->mAttribs		= tag.mAttribs;
		new_tag->mbIsText		= tag.mbIsText;
		new_tag->mbIsControl	= tag.mbIsControl;

		ctx.construction_stack.back()->mChildren.push_back(new_tag);
		ctx.construction_stack.push_back(new_tag);

		output_tag_contents(ctx, out, tag);

		ctx.construction_stack.pop_back();
	} else if (tag.mbIsText) {
		if (out) {
			if (tag.mbIsControl) {
				*out += tag.mName;
			} else if (ctx.cdata_count) {
				if (ctx.pre_count) {
					*out += tag.mName;
				} else {
					for(const char c : tag.mName) {
						if (isspace(c)) {
							ctx.holding_space = true;
						} else {
							if (ctx.eat_next_space) {
								ctx.eat_next_space = false;
							}
							else if (ctx.holding_space) {
								*out += ' ';
							}
							ctx.holding_space = false;
							*out += c;
						}
					}
				}
			} else {
				for(const char c : tag.mName) {
					if (!isspace(c)) {
						error(ctx, "inline text not allowed");
					}
				}
			}
		}
	} else {
		bool cdata = tag.SupportsCDATA();

		if (cdata) {
			if (!ctx.cdata_count) {
				ctx.holding_space = false;
				ctx.eat_next_space = true;
			}
			++ctx.cdata_count;
		}

		if (!out) {
			output_tag_contents(ctx, out, tag);
		} else if (tag.mChildren.empty()) {
			*out += '<';
			*out += tag.mName;
			output_tag_attributes(*out, tag);
			*out += '>';
		} else {
			*out += '<';
			*out += tag.mName;
			output_tag_attributes(*out, tag);
			*out += '>';

			output_tag_contents(ctx, out, tag);

			*out += "</";
			*out += tag.mName;
			*out += '>';
		}

		if (cdata)
			--ctx.cdata_count;
	}
	if (!tag.mbIsText && tag.mName == "pre")
		--ctx.pre_count;
}

std::string HTMLize(const std::string& s)
{
	std::string t;

	for(const char c : s) {
		switch(c) {
		case '"':	t.append("&quot;"); break;
		case '<':	t.append("&lt;"); break;
		case '>':	t.append("&gt;"); break;
		case '&':	t.append("&amp;"); break;
		default:	t += c; break;
		}
	}

	return t;
}

void output_source_tags(Context& ctx, std::string *out, const TreeNode& tag) {
	std::string s;

	if (tag.mbIsText)
		s = tag.mName;
	else if (tag.mChildren.empty()) {
		s = '<';
		s += tag.mName;
		output_tag_attributes(s, tag);
		s += '>';
	} else {
		s = '<';
		s += tag.mName;
		output_tag_attributes(s, tag);
		s += '>';

		out->append(HTMLize(s));

		out->append("<ul marker=none>");

		for(const auto& child : tag.mChildren) {
			out->append("<li>");
			output_source_tags(ctx, out, *child);
			out->append("</li>");
		}

		out->append("</ul>");

		s = "</";
		s += tag.mName;
		s += '>';
	}

	out->append(HTMLize(s));

	if (!tag.mbIsText)
		out->append("<br>");
}

void dump_stack(Context& ctx)
{
	printf("Current execution stack:\n");
	int indent = 3;
	for (const auto item : ctx.stack | std::views::reverse) {
		const TreeNode& tag = *item;
		printf("%*c%s(%d): processing <%s>\n", indent, ' ', tag.mpLocation->mName.c_str(), tag.mLineno, tag.mName.c_str());
		indent += 3;
	}

	indent = 3;
	for (const auto item : ctx.construction_stack | std::views::reverse) {
		const TreeNode& tag = *item;
		printf("%*c%s(%d): while creating tag <%s>\n", indent, ' ', tag.mpLocation->mName.c_str(), tag.mLineno, tag.mName.c_str());
		indent += 3;
	}

	indent = 3;
	for (const auto item : ctx.invocation_stack | std::views::reverse) {
		const TreeNode& tag = *item;
		printf("%*c%s(%d): while invoked from tag <%s>\n", indent, ' ', tag.mpLocation->mName.c_str(), tag.mLineno, tag.mName.c_str());
		indent += 3;
	}
}

void output_toc_children(FILE *f, const TreeNode& node);

void output_toc_node(FILE *f, const TreeNode& node) {
	if (node.mName != "tocnode")
		error("Invalid node <%s> found during HTML help TOC generation", node.mName.c_str());

	const TreeAttribute *attrib = node.Attrib("name");

	if (!attrib || attrib->mbNoValue)
		error("<tocnode> must have NAME attribute");

	const TreeAttribute *target = node.Attrib("target");

	fputs("<LI><OBJECT type=\"text/sitemap\">\n", f);
	fprintf(f, "<param name=\"Name\" value=\"%s\">\n", HTMLize(attrib->mValue).c_str());
	if (target && !target->mbNoValue)
		fprintf(f, "<param name=\"Local\" value=\"%s\">\n", HTMLize(target->mValue).c_str());
	fputs("</OBJECT>\n", f);

	output_toc_children(f, node);
}

void output_toc_children(FILE *f, const TreeNode& node)
{
	bool nodesFound = false;

	for(const auto& childNode : node.mChildren) {
		if (!childNode->mbIsText) {
			if (!nodesFound) {
				nodesFound = true;
				fputs("<UL>\n", f);
			}

			output_toc_node(f, *childNode);
		}
	}

	if (nodesFound) {
		fputs("</UL>\n", f);
	}
}

void output_toc(FILE *f, const TreeNode& root) {
	fputs(	"<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML//EN\">\n"
			"<HTML>\n"
			"<HEAD>\n"
			"<!-- Sitemap 1.0 -->\n"
			"</HEAD><BODY>\n"
			"<OBJECT type=\"text/site properties\">\n"
			"\t<param name=\"ImageType\" value=\"Folder\">\n"
			"</OBJECT>\n"
		, f);

	output_toc_children(f, root);

	fputs(	"</BODY>\n"
			"</HTML>\n"
		, f);
}

void output_special_tag(Context& ctx, std::string *out, const TreeNode& tag) {
	if (tag.mName == "lina:fireball") {
		const TreeAttribute *a1 = tag.Attrib("src");
		const TreeAttribute *a2 = tag.Attrib("dst");

		if (!a1 || !a2)
			error(ctx, "<lina:fireball> requires SRC and DST attributes");

		g_fileCopies[a2->mValue] = a1->mValue;
	} else if (tag.mName == "lina:write") {
		const TreeAttribute *a = tag.Attrib("file");

		if (!a)
			error(ctx, "<lina:write> must specify FILE");

		std::string s;

		std::list<TreeNode *> tempStack;
		ctx.construction_stack.swap(tempStack);
		int cdataCount = ctx.cdata_count;
		int preCount = ctx.pre_count;
		ctx.cdata_count = 0;
		ctx.pre_count = 0;
		bool bHoldingSpace = ctx.holding_space;
		bool bEatNextSpace = ctx.eat_next_space;
		ctx.holding_space = false;
		ctx.eat_next_space = true;
		output_tag_contents(ctx, &s, tag);
		ctx.holding_space = bHoldingSpace;
		ctx.eat_next_space = bEatNextSpace;
		ctx.pre_count = cdataCount;
		ctx.cdata_count = preCount;
		ctx.construction_stack.swap(tempStack);

		std::string filename(create_output_filename(a->mValue));

		FILE *f = nullptr;
		errno_t err = fopen_s(&f, filename.c_str(), "wb");
		if (err) {
			error(ctx, "couldn't create \"%s\"", a->mValue.c_str());
		}
		fwrite(s.data(), s.length(), 1, f);
		fclose(f);

		printf("created file: %s\n", a->mValue.c_str());
	} else if (tag.mName == "lina:body") {

//		printf("outputting:\n");
//		dump_parse_tree(*ctx.invocation_stack.back(), 4);

		output_tag_contents(ctx, out, *ctx.invocation_stack.back());
	} else if (tag.mName == "lina:tag") {
		const TreeAttribute *a = tag.Attrib("name");
		if (!a)
			error(ctx, "<lina:tag> must have NAME attribute");

		ctx.construction_stack.push_back(ctx.mpDocument->AllocNode());
		TreeNode *new_tag = ctx.construction_stack.back();

		new_tag->mpLocation = tag.mpLocation;
		new_tag->mLineno = tag.mLineno;
		new_tag->mName = a->mValue;
		new_tag->mbIsText = false;
		new_tag->mbIsControl = false;

		// compatibility
		if (!new_tag->mName.compare(0, 2, "w:"))
			new_tag->mName.replace(0, 2, "lina:");

		output_tag_contents(ctx, NULL, tag);

		ctx.construction_stack.pop_back();
		output_tag(ctx, out, *new_tag);
	} else if (tag.mName == "lina:arg") {
		if (!out && ctx.construction_stack.empty())
			error(ctx, "<lina:arg> can only be used in an output context");
		const TreeAttribute *a = tag.Attrib("name");
		if (!a)
			error(ctx, "<lina:arg> must have NAME attribute");

		if (ctx.invocation_stack.empty())
			error(ctx, "<lina:arg> can only be used during macro expansion");

		auto it(ctx.invocation_stack.cend());
		--it;

		int levels = 1;
		const char *name = a->mValue.c_str();
		while(*name == '^') {
			++levels;
			++name;

			if (it == ctx.invocation_stack.cbegin()) {
				error(ctx, "Number of up-scope markers in name exceeds macro nesting level");
			}

			--it;
		}

		const TreeNode& macrotag = **it;
		const TreeAttribute *a2 = macrotag.Attrib(name);
		if (!a2)
			error(ctx, "macro invocation <%s> does not have an attribute \"%s\"", macrotag.mName.c_str(), name);

		if (out) {
			*out += a2->mValue;

			ctx.eat_next_space = false;
			ctx.holding_space = false;
		} else {
			TreeNode *t = ctx.mpDocument->AllocNode();

			t->mpLocation = tag.mpLocation;
			t->mLineno = tag.mLineno;
			t->mbIsControl = false;
			t->mbIsText = true;
			t->mName = a2->mValue;

			ctx.construction_stack.back()->mChildren.push_back(t);
		}
	} else if (tag.mName == "lina:if-arg") {
		const TreeAttribute *a = tag.Attrib("name");
		if (!a)
			error(ctx, "<lina:if-arg> must have NAME attribute");

		if (ctx.invocation_stack.empty())
			error(ctx, "<lina:if-arg> can only be used during macro expansion");

		const TreeNode& macrotag = *ctx.invocation_stack.back();
		const TreeAttribute *a2 = macrotag.Attrib(a->mValue);
		if (a2)
			output_tag_contents(ctx, out, tag);
	} else if (tag.mName == "lina:if-not-arg") {
		const TreeAttribute *a = tag.Attrib("name");
		if (!a)
			error(ctx, "<lina:if-not-arg> must have NAME attribute");

		if (ctx.invocation_stack.empty())
			error(ctx, "<lina:if-not-arg> can only be used during macro expansion");

		const TreeNode& macrotag = *ctx.invocation_stack.back();
		const TreeAttribute *a2 = macrotag.Attrib(a->mValue);
		if (!a2)
			output_tag_contents(ctx, out, tag);
	} else if (tag.mName == "lina:attrib") {
		if (ctx.construction_stack.empty())
			error(ctx, "<lina:attrib> can only be used in a <lina:tag> element");

		const TreeAttribute *a = tag.Attrib("name");
		if (!a)
			error(ctx, "<lina:attrib> must have NAME attribute");

		std::string s;
		std::list<TreeNode *> tempStack;
		ctx.construction_stack.swap(tempStack);
		++ctx.cdata_count;
		++ctx.pre_count;
		bool bHoldingSpace = ctx.holding_space;
		bool bEatNextSpace = ctx.eat_next_space;
		ctx.holding_space = false;
		ctx.eat_next_space = true;
		output_tag_contents(ctx, &s, tag);
		ctx.holding_space = bHoldingSpace;
		ctx.eat_next_space = bEatNextSpace;
		--ctx.pre_count;
		--ctx.cdata_count;
		ctx.construction_stack.swap(tempStack);

		TreeNode *t = ctx.construction_stack.back();
		TreeAttribute new_att;
		if (tag.Attrib("novalue")) {
			new_att.mbNoValue = true;
		} else {
			new_att.mbNoValue = false;
			new_att.mValue = s;
		}
		new_att.mName = a->mValue;
		t->mAttribs.push_back(new_att);
	} else if (tag.mName == "lina:pull") {
		if (ctx.invocation_stack.empty())
			error(ctx, "<lina:pull> can only be used during macro expansion");

		const TreeAttribute *a = tag.Attrib("name");
		if (!a)
			error(ctx, "<lina:pull> must have NAME attribute");

		const TreeNode *t = ctx.find_tag(a->mValue);
		
		if (!t)
			error(ctx, "cannot find tag <%s> referenced in <lina:pull>", a->mValue.c_str());

		output_tag_contents(ctx, out, *t);		
	} else if (tag.mName == "lina:for-each") {
		const TreeAttribute *a = tag.Attrib("name");
		if (!a)
			error(ctx, "<lina:for-each> must have NAME attribute");
		
		std::string node_name;
		const TreeNode *parent;
		if (ctx.invocation_stack.empty()) {
			if (!a->mValue.empty() && a->mValue[0] == '/') {
				parent = ctx.mpDocument->mpRoot->ResolvePath(a->mValue.substr(1), node_name);
			} else {
				error(ctx, "path must be absolute if not in macro context");
			}
		} else {
			for (const auto item : ctx.invocation_stack | std::views::reverse) {
				parent = item->ResolvePath(a->mValue, node_name);
				if (parent) {
					break;
				}
				if (!a->mValue.empty() && a->mValue[0] == '/') {
					break;
				}
			}
		}

		if (!parent) {
			error(ctx, "cannot resolve path \"%s\"", a->mValue.c_str());
		}

		ctx.invocation_stack.push_back(NULL);
		for(const auto child : parent->mChildren) {
			if (child->mName == node_name) {
				ctx.invocation_stack.back() = child;
				output_tag_contents(ctx, out, tag);
			}
		}
		ctx.invocation_stack.pop_back();
	} else if (tag.mName == "lina:apply") {
		const TreeAttribute *a = tag.Attrib("name");
		if (!a) {
			error(ctx, "<lina:apply> must have NAME attribute");
		}

		std::map<std::string, TreeNode *>::const_iterator it(ctx.mpDocument->mMacros.find(a->mValue));

		if (it == ctx.mpDocument->mMacros.end()) {
			error(ctx, "macro \"%s\" undeclared", a->mValue.c_str());
		}
		
		ctx.invocation_stack.push_back(NULL);
		for(const auto& child : tag.mChildren) {
			if (!child->mbIsText) {
				ctx.invocation_stack.back() = child;
				output_tag_contents(ctx, out, *(*it).second);
			}
		}
		ctx.invocation_stack.pop_back();
	} else if (tag.mName == "lina:if-present") {
		if (ctx.invocation_stack.empty())
			error(ctx, "<lina:if-present> can only be used during macro expansion");
		const TreeAttribute *a = tag.Attrib("name");
		if (!a)
			error(ctx, "<lina:if-present> must have NAME attribute");

		const TreeNode *t = ctx.find_tag(a->mValue);
		if (t)
			output_tag_contents(ctx, out, tag);
	} else if (tag.mName == "lina:if-not-present") {
		if (ctx.invocation_stack.empty())
			error(ctx, "<lina:if-not-present> can only be used during macro expansion");
		const TreeAttribute *a = tag.Attrib("name");
		if (!a)
			error(ctx, "<lina:if-not-present> must have NAME attribute");

		const TreeNode *t = ctx.find_tag(a->mValue);
		if (!t)
			output_tag_contents(ctx, out, tag);
	} else if (tag.mName == "lina:pre") {
		++ctx.pre_count;
		++ctx.cdata_count;
		if (!out)
			output_standard_tag(ctx, out, tag);
		else {
			output_tag_contents(ctx, out, tag);
		}
		--ctx.cdata_count;
		--ctx.pre_count;
	} else if (tag.mName == "lina:cdata") {
		++ctx.cdata_count;
		if (!out)
			output_standard_tag(ctx, out, tag);
		else
			output_tag_contents(ctx, out, tag);
		--ctx.cdata_count;
	} else if (tag.mName == "lina:delay") {
		for(const auto& child : tag.mChildren) {
			output_standard_tag(ctx, out, *child);
		}
	} else if (tag.mName == "lina:dump-stack") {
		dump_stack(ctx);
	} else if (tag.mName == "lina:replace") {
		const TreeAttribute *a = tag.Attrib("from");
		if (!a || a->mbNoValue)
			error(ctx, "<lina:replace> must have FROM attribute");
		const TreeAttribute *a2 = tag.Attrib("to");
		if (!a2 || a2->mbNoValue)
			error(ctx, "<lina:replace> must have TO attribute");

		const std::string& x = a->mValue;
		const std::string& y = a2->mValue;

		std::string s, t;
		std::string::size_type i = 0;

		output_tag_contents(ctx, &s, tag);

		for(;;) {
			std::string::size_type j = s.find(x, i);
			if (j != i)
				t.append(s, i, j-i);
			if (j == std::string::npos)
				break;
			t.append(y);
			i = j + x.size();
		}

		TreeNode *new_tag = ctx.mpDocument->AllocNode();

		new_tag->mpLocation = tag.mpLocation;
		new_tag->mLineno = tag.mLineno;
		new_tag->mbIsText = true;
		new_tag->mbIsControl = false;
		new_tag->mName = t;

		output_tag(ctx, out, *new_tag);
	} else if (tag.mName == "lina:set-option") {
		const TreeAttribute *a_name = tag.Attrib("name");
		if (!a_name)
			error(ctx, "<lina:set-option> must have NAME attribute");

		if (a_name->mValue == "link-truncate") {
			const TreeAttribute *a_val = tag.Attrib("baseurl");
			if (!a_val || a_val->mbNoValue)
				error(ctx, "option \"link-truncate\" requires BASEURL attribute");

			bool bTruncate = !tag.Attrib("notruncate");

			g_truncateURLs.push_back(std::make_pair(a_val->mValue, bTruncate));
		} else if (a_name->mValue == "output-dir") {
			const TreeAttribute *a_val = tag.Attrib("target");
			if (!a_val || a_val->mbNoValue)
				error(ctx, "option \"output-dir\" requires TARGET attribute");

			g_outputDir = a_val->mValue;
		} else if (a_name->mValue == "tag-info") {
			const TreeAttribute *a_tagname = tag.Attrib("tag");
			if (!a_tagname || a_tagname->mbNoValue)
				error(ctx, "option \"tag-info\" requires TAG attribute");

			const TreeAttribute *a_cdata = tag.Attrib("cdata");

			if (!a_cdata || a_cdata->mbNoValue)
				error(ctx, "option \"tag-info\" requires CDATA attribute");

			TreeNode::SetSupportsCDATA(a_tagname->mValue, is_true(a_cdata->mValue));
		} else
			error(ctx, "option \"%s\" unknown\n", a_name->mValue.c_str());

	} else if (tag.mName == "lina:data") {
		// do nothing
	} else if (tag.mName == "lina:source") {
		if (out) {
			for(const auto& child : tag.mChildren) {
				output_source_tags(ctx, out, *child);
			}
		}
	} else if (tag.mName == "lina:htmlhelp-toc") {
		const TreeAttribute *a_val = tag.Attrib("file");
		if (!a_val || a_val->mbNoValue)
			error(ctx, "<lina:htmlhelp-toc> requires FILE attribute");

		const std::string filename(create_output_filename(a_val->mValue));

		// build new tag with TOC contents
		ctx.construction_stack.push_back(ctx.mpDocument->AllocNode());
		TreeNode *new_tag = ctx.construction_stack.back();

		new_tag->mpLocation = tag.mpLocation;
		new_tag->mLineno = tag.mLineno;
		new_tag->mName = a_val->mValue;
		new_tag->mbIsText = false;
		new_tag->mbIsControl = false;

		output_tag_contents(ctx, NULL, tag);

		ctx.construction_stack.pop_back();
		output_tag(ctx, out, *new_tag);

		FILE *f = nullptr;
		errno_t err = fopen_s(&f, filename.c_str(), "wb");
		if (err) {
			error(ctx, "couldn't create htmlhelp toc \"%s\"", a_val->mValue.c_str());
		}
		output_toc(f, *new_tag);
		fclose(f);

	} else if (tag.mName == "lina:htmlhelp-project") {
		const TreeAttribute *file_val = tag.Attrib("file");
		if (!file_val || file_val->mbNoValue)
			error(ctx, "<lina:htmlhelp-project> requires FILE attribute");

		const TreeAttribute *output_val = tag.Attrib("output");
		if (!output_val || output_val->mbNoValue)
			error(ctx, "<lina:htmlhelp-project> requires OUTPUT attribute");

		const TreeAttribute *toc_val = tag.Attrib("toc");
		if (!toc_val || toc_val->mbNoValue)
			error(ctx, "<lina:htmlhelp-project> requires TOC attribute");

		const TreeAttribute *title_val = tag.Attrib("title");
		if (!title_val || title_val->mbNoValue)
			error(ctx, "<lina:htmlhelp-project> requires TITLE attribute");

		const std::string filename(create_output_filename(file_val->mValue));

		FILE *f = nullptr;
		errno_t err = fopen_s(&f, filename.c_str(), "wb");
		if (err) {
			error(ctx, "couldn't create htmlhelp project \"%s\"", file_val->mValue.c_str());
		}
		fprintf(f,
			"[OPTIONS]\n"
			"Auto Index=Yes\n"
			"Compatibility=1.1 or later\n"
			"Compiled file=%s\n"
			"Contents file=%s\n"
			"Default topic=index.html\n"
			"Display compile progress=no\n"
			"Full-text search=Yes\n"
			, output_val->mValue.c_str()
			, toc_val->mValue.c_str()
			);


		const TreeAttribute *fullstop_val = tag.Attrib("fullstop");
		if (fullstop_val && !fullstop_val->mbNoValue)
			fprintf(f, "Full text search stop list file=%s\n", fullstop_val->mValue.c_str());

		fprintf(f,
			"Language=0x0409 English (United States)\n"
			"Title=%s\n"
			"\n"
			"[FILES]\n"
			, title_val->mValue.c_str()
			);

		for(const auto helpgile : g_htmlHelpFiles) {
			fprintf(f, "%s\n", helpgile.c_str());
		}

		fclose(f);
	} else if (tag.mName == "lina:htmlhelp-addfile") {
		const TreeAttribute *file_val = tag.Attrib("file");
		if (!file_val || file_val->mbNoValue)
			error(ctx, "<lina:htmlhelp-addfile> requires FILE attribute");

		g_htmlHelpFiles.push_back(file_val->mValue);
	} else {
		std::string macroName(tag.mName, 5, std::string::npos);
		std::map<std::string, TreeNode *>::const_iterator it = ctx.mpDocument->mMacros.find(macroName);

		if (it == ctx.mpDocument->mMacros.end())
			error(ctx, "macro <lina:%s> not found", macroName.c_str());

//		dump_stack(ctx);
//		printf("executing macro: %s (%s:%d)\n", tag.mName.c_str(), tag.mLocation->name.c_str(), tag.mLineno);

		ctx.invocation_stack.push_back(&tag);
		output_tag_contents(ctx, out, *(*it).second);
		ctx.invocation_stack.pop_back();

//		printf("exiting macro: %s (%s:%d)\n", tag.mName.c_str(), tag.mLocation->name.c_str(), tag.mLineno);
	}
}

void output_tag(Context& ctx, std::string *out, const TreeNode& tag) {
	ctx.stack.push_back(&tag);

	if (!tag.mbIsText && !tag.mName.compare(0,5,"lina:")) {
		output_special_tag(ctx, out, tag);
	} else {
		output_standard_tag(ctx, out, tag);
	}

	ctx.stack.pop_back();
}

int main(int argc, char **argv)
{
	TreeNode::SetSupportsCDATA("p",			true);
	TreeNode::SetSupportsCDATA("h1",		true);
	TreeNode::SetSupportsCDATA("h2",		true);
	TreeNode::SetSupportsCDATA("h3",		true);
	TreeNode::SetSupportsCDATA("h4",		true);
	TreeNode::SetSupportsCDATA("h5",		true);
	TreeNode::SetSupportsCDATA("h6",		true);
	TreeNode::SetSupportsCDATA("td",		true);
	TreeNode::SetSupportsCDATA("th",		true);
	TreeNode::SetSupportsCDATA("li",		true);
	TreeNode::SetSupportsCDATA("style",		true);
	TreeNode::SetSupportsCDATA("script",	true);
	TreeNode::SetSupportsCDATA("title",		true);
	TreeNode::SetSupportsCDATA("div",		true);
	TreeNode::SetSupportsCDATA("dt",		true);
	TreeNode::SetSupportsCDATA("dd",		true);

	TreeDocument doc;
	TreeParser parser(&doc);

	parser.ParseFile(argv[1]);

//	dump_parse_tree(*doc.mpRoot);

	Context ctx;

	ctx.mpDocument = &doc;

	output_tag(ctx, NULL, *doc.mpRoot);

	// copy files

	for(const auto& [dstfile, srcfile] : g_fileCopies) {
		copy_file(dstfile, srcfile);
	}

	printf("No errors.\n");
	return 0;
}
