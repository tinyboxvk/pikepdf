/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (C) 2021, James R. Barlow (https://github.com/jbarlow83/)
 */

#include <sstream>
#include <locale>

#include "pikepdf.h"
#include "parsers.h"

void PyParserCallbacks::handleObject(QPDFObjectHandle h)
{
    PYBIND11_OVERLOAD_PURE_NAME(void,
        QPDFObjectHandle::ParserCallbacks,
        "handle_object", /* Python name */
        handleObject,    /* C++ name */
        h);
}

void PyParserCallbacks::handleEOF()
{
    PYBIND11_OVERLOAD_PURE_NAME(void,
        QPDFObjectHandle::ParserCallbacks,
        "handle_eof", /* Python name */
        handleEOF,    /* C++ name; trailing comma needed for macro */
    );
}

OperandGrouper::OperandGrouper(const std::string &operators)
    : parsing_inline_image(false), count(0)
{
    std::istringstream f(operators);
    f.imbue(std::locale::classic());
    std::string s;
    while (std::getline(f, s, ' ')) {
        this->whitelist.insert(s);
    }
}

void OperandGrouper::handleObject(QPDFObjectHandle obj)
{
    this->count++;
    if (obj.getTypeCode() == QPDFObject::object_type_e::ot_operator) {
        std::string op = obj.getOperatorValue();

        // If we have a whitelist and this operator is not on the whitelist,
        // discard it and all the tokens we collected
        if (!this->whitelist.empty()) {
            if (op[0] == 'q' || op[0] == 'Q') {
                // We have token with multiple stack push/pops
                if (this->whitelist.count("q") == 0 &&
                    this->whitelist.count("Q") == 0) {
                    this->tokens.clear();
                    return;
                }
            } else if (this->whitelist.count(op) == 0) {
                this->tokens.clear();
                return;
            }
        }
        if (op == "BI") {
            this->parsing_inline_image = true;
        } else if (this->parsing_inline_image) {
            if (op == "ID") {
                this->inline_metadata = this->tokens;
            } else if (op == "EI") {
                auto PdfInlineImage =
                    py::module_::import("pikepdf").attr("PdfInlineImage");
                auto kwargs            = py::dict();
                kwargs["image_data"]   = this->tokens.at(0);
                kwargs["image_object"] = this->inline_metadata;
                auto iimage            = PdfInlineImage(**kwargs);

                // Package as list with single element for consistency
                auto iimage_list = py::list();
                iimage_list.append(iimage);

                auto instruction = py::make_tuple(
                    iimage_list, QPDFObjectHandle::newOperator("INLINE IMAGE"));
                this->instructions.append(instruction);

                this->parsing_inline_image = false;
                this->inline_metadata.clear();
            }
        } else {
            py::list operand_list = py::cast(this->tokens);
            auto instruction      = py::make_tuple(operand_list, obj);
            this->instructions.append(instruction);
        }
        this->tokens.clear();
    } else {
        this->tokens.push_back(obj);
    }
}

void OperandGrouper::handleEOF()
{
    if (!this->tokens.empty())
        this->warning = "Unexpected end of stream";
}

py::list OperandGrouper::getInstructions() const { return this->instructions; }
std::string OperandGrouper::getWarning() const { return this->warning; }