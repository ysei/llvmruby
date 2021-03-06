#include "llvmruby.h"
#include <sstream>

extern "C" {
VALUE 
llvm_function_wrap(Function *f) { 
  return Data_Wrap_Struct(cLLVMFunction, NULL, NULL, f); 
}

VALUE 
llvm_function_create_block(VALUE self) {
  BasicBlock *bb = BasicBlock::Create(getGlobalContext(), "bb", LLVM_FUNCTION(self));
  return llvm_basic_block_wrap(bb);
}

VALUE
llvm_function_arguments(VALUE self) {
  Function *f = LLVM_FUNCTION(self);
  VALUE arg_array = rb_ary_new();
  Function::arg_iterator args = f->arg_begin();
  while(args != f->arg_end()) {
    Value *arg = args++;
    rb_ary_push(arg_array, llvm_value_wrap(arg));
  }
  return arg_array;
}

VALUE
llvm_function_inspect(VALUE self) {
  Function *f = LLVM_FUNCTION(self);
  std::string str;
  raw_string_ostream strstrm(str);
  strstrm << *f;
  return rb_str_new2(str.c_str());
}

VALUE
llvm_function_get_basic_block_list(VALUE self) {
  Function *f = LLVM_FUNCTION(self);
  VALUE bb_array = rb_ary_new();
  if(!f)
	return bb_array;
	
  Function::iterator bbs = f->begin();
  while(bbs != f->end()) {
    BasicBlock *bb = bbs++;
    rb_ary_push(bb_array, llvm_basic_block_wrap(bb));
  }
  return bb_array;
}

}
