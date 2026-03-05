#pragma once

#include <Arduino.h>
#include "state.h"

uint8_t execute_vm(int32_t t);
String decompile(bool to_rpn);

bool compileRPN(String input);
bool compileInfix(String input, bool reset_t);

void saveUndo();
String getOpSym(OpCode op);
int getPrecedence(OpCode op);