#include "llvmruby.h"
#include "llvm/Assembly/Parser.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Target/TargetSelect.h"
#include "llvm/Function.h"

#include <fstream>
#include <sstream>
#include <iostream>
using namespace std;

extern "C" {

VALUE
llvm_module_allocate(VALUE klass) {
  return Data_Wrap_Struct(klass, NULL, NULL, NULL);
}

VALUE
llvm_module_initialize(VALUE self, VALUE rname) {
  Check_Type(rname, T_STRING);
  DATA_PTR(self) = new Module(StringValuePtr(rname), getGlobalContext());
  return self;
}

VALUE
llvm_module_get_or_insert_function(VALUE self, VALUE name, VALUE rtype) {
  Check_Type(name, T_STRING);
  CHECK_TYPE(rtype, cLLVMFunctionType);

  Module *m = LLVM_MODULE(self);
  FunctionType *type = LLVM_FUNC_TYPE(rtype);
  Constant *fn = m->getOrInsertFunction(StringValuePtr(name), type);

#if defined(USE_ASSERT_CHECK)
  if (isa<Function>(fn) == 0) {
    rb_raise(rb_eRuntimeError, 
	     "cast<Function>(fn) argument of incompatible type !");
  }
#endif

  Function *f = cast<Function>(fn);
  return llvm_function_wrap(f); 
}

VALUE
llvm_module_get_function(VALUE self, VALUE name) {
  Check_Type(name, T_STRING);
  Module *m = LLVM_MODULE(self);

  Function *f = NULL;

  if(m)
    f = m->getFunction(StringValuePtr(name));

  return llvm_function_wrap(f);
}

VALUE
llvm_module_global_constant(VALUE self, VALUE rtype, VALUE rinitializer) {
  Module *m = LLVM_MODULE(self);
  Type *type = LLVM_TYPE(rtype);
  Constant *initializer = (Constant*)DATA_PTR(rinitializer);
  GlobalVariable *gv = new GlobalVariable(type, true, GlobalValue::InternalLinkage, initializer, "", m);
  return llvm_value_wrap(gv);
}

VALUE
llvm_module_global_variable(VALUE self, VALUE rtype, VALUE rinitializer) {
  Module *m = LLVM_MODULE(self);
  Type *type = LLVM_TYPE(rtype);
  Constant *initializer = (Constant*)DATA_PTR(rinitializer);
  GlobalVariable *gv = new GlobalVariable(type, false, GlobalValue::InternalLinkage, initializer, "", m);
  return llvm_value_wrap(gv);
}


VALUE
llvm_module_inspect(VALUE self) {
  Module *m = LLVM_MODULE(self);
  if(!m)
	return rb_str_new2("Module is null");
	
  std::string str;
  raw_string_ostream strstrm(str);
  strstrm << *m;
  return rb_str_new2(str.c_str());
}

VALUE
llvm_pass_manager_allocate(VALUE klass) {
  return Data_Wrap_Struct(klass, NULL, NULL, NULL);
}

VALUE
llvm_pass_manager_initialize(VALUE self) {
  PassManager *pm = new PassManager;
  DATA_PTR(self) = pm;
  return self;
}

VALUE
llvm_pass_manager_run(VALUE self, VALUE module) {
  PassManager *pm = (PassManager*) DATA_PTR(self);
  Module *m = LLVM_MODULE(module);
  
  pm->add(new TargetData(m));
  pm->add(createVerifierPass());
  pm->add(createLowerSetJmpPass());
  pm->add(createCFGSimplificationPass());
  pm->add(createPromoteMemoryToRegisterPass());
  pm->add(createGlobalOptimizerPass());
  pm->add(createGlobalDCEPass());
  pm->add(createFunctionInliningPass());
  
  pm->run(*m);
  return Qtrue;
}

static ExecutionEngine *EE = NULL;

VALUE
llvm_execution_engine_get(VALUE klass, VALUE module) {
  CHECK_TYPE(module, cLLVMModule);

#if defined(__CYGWIN__)

  // Load dll Modules for ruby
  sys::DynamicLibrary::LoadLibraryPermanently("cygwin1.dll");
  sys::DynamicLibrary::LoadLibraryPermanently("cygruby190.dll");

#endif

  Module *m = LLVM_MODULE(module);
  
  if(EE == NULL) {
	InitializeNativeTarget();
	LLVMLinkInJIT(); // Forcing linking to JIT
	string errStr;
    EE = EngineBuilder(m).setErrorStr(&errStr).setEngineKind(EngineKind::JIT).create();
    if(!errStr.empty()) {
      cerr << "ExecutionEngine.get - Error: " << errStr << endl;
    }
  }

  return Qtrue;
}

VALUE
llvm_module_external_function(VALUE self, VALUE name, VALUE type) {
  Check_Type(name, T_STRING);
  CHECK_TYPE(type, cLLVMFunctionType);

  Module *module = LLVM_MODULE(self);
  Function *f = Function::Create(
    LLVM_FUNC_TYPE(type), 
    Function::ExternalLinkage, 
    StringValuePtr(name),
    module
  );
  return Data_Wrap_Struct(cLLVMFunction, NULL, NULL, f);
}

VALUE
llvm_module_read_assembly(VALUE self, VALUE assembly) {
  Check_Type(assembly, T_STRING);

  const char * asmString = StringValuePtr(assembly);

  SMDiagnostic e;
  Module *module = ParseAssemblyString(
    asmString,
    0,
    e,
    getGlobalContext()
  );

  if(!module) {
    VALUE exception = rb_exc_new2(cLLVMAssemblySyntaxError, e.getMessage().c_str());
    rb_iv_set(exception, "@line", INT2NUM(e.getLineNo()));
    rb_iv_set(exception, "@column", INT2NUM(e.getColumnNo()));
    rb_iv_set(exception, "@line_contents", rb_str_new2(e.getLineContents().c_str()));
    rb_iv_set(exception, "@filename", rb_str_new2(""));
    rb_exc_raise(exception);
  }

  return Data_Wrap_Struct(cLLVMModule, NULL, NULL, module);
}

VALUE
llvm_module_read_bitcode(VALUE self, VALUE bitcode) {
  Check_Type(bitcode, T_STRING);

#if defined(RSTRING_PTR)
  OwningPtr<MemoryBuffer> buf(MemoryBuffer::getMemBufferCopy(StringRef(RSTRING_PTR(bitcode), RSTRING_LEN(bitcode))));
#else
  OwningPtr<MemoryBuffer> buf(MemoryBuffer::getMemBufferCopy(StringRef(RSTRING(bitcode)->ptr, RSTRING(bitcode)->len)));
#endif

  string err;
  Module *module = ParseBitcodeFile(buf.take(), getGlobalContext(), &err);

  if(!module) {
    VALUE exception = rb_exc_new2(rb_eSyntaxError, err.c_str());
    rb_exc_raise(exception);
  }

  return Data_Wrap_Struct(cLLVMModule, NULL, NULL, module);
}


VALUE
llvm_module_write_bitcode(VALUE self, VALUE file_name) {
  Check_Type(file_name, T_STRING);

  // Don't really know how to handle c++ streams well, 
  // dumping all into string buffer and then saving
  std::string error;
  raw_fd_ostream file(StringValuePtr(file_name), error);

  WriteBitcodeToFile(LLVM_MODULE(self), file);   // Convert value into a string.
  return Qtrue;
}

GenericValue
Val2GV(const VALUE& val, const Type * targetType) {
  GenericValue gv;

  switch(targetType->getTypeID()) {
    case Type::VoidTyID:
      cout << "This is void!" << endl;
      break;
    case Type::FloatTyID:
      gv.FloatVal = NUM2DBL(val);
      break;
    case Type::DoubleTyID:
      gv.DoubleVal = NUM2DBL(val);
      break;
    case Type::IntegerTyID:
      if(TYPE(val) != T_TRUE && TYPE(val) != T_FALSE) {
        if(TYPE(val) != T_NIL) {
          gv.IntVal = APInt(sizeof(long)*8, NUM2LONG(val), true);
        } else {
          gv.IntVal = APInt(sizeof(long)*8, 0);
        }
      } else {
        gv.IntVal = (TYPE(val) == T_TRUE);
      }
      break;
    case Type::PointerTyID:
      if(TYPE(val) == T_STRING) {
        gv.PointerVal = RSTRING(val);
      } else if(TYPE(val) == T_STRUCT) {
        gv.PointerVal = RSTRUCT(val);
      } else if(TYPE(val) == T_ARRAY) {
        gv.PointerVal = RARRAY(val);
      } else if(TYPE(val) == T_HASH) {
        gv.PointerVal = RHASH(val);
      } else if(TYPE(val) == T_CLASS) {
        gv.PointerVal = RCLASS(val);
      } else if(TYPE(val) == T_OBJECT) {
        gv.PointerVal = ROBJECT(val);
      } else if(TYPE(val) == T_DATA) {
        gv.PointerVal = RDATA(val);
      } else if(TYPE(val) == T_NIL){
        gv.PointerVal = 0;
      } else {
        rb_raise(rb_eArgError, "Can't convert pointer into GenericValue. That type is not supported.");
      }
      break;
    default:
      rb_raise(rb_eArgError, "Can't convert VALUE into GenericValue");
      break;
  };
  
  return gv;
}

VALUE
Gv2Val(const GenericValue& gv, const Type * targetType) {
  if(targetType->getTypeID() == Type::FloatTyID) {
    return rb_float_new(gv.FloatVal);
  } else if(targetType->getTypeID() == Type::DoubleTyID) {
    return rb_float_new(gv.DoubleVal);
  } else if(targetType->getTypeID() == Type::IntegerTyID) {
    return INT2NUM(gv.IntVal.getSExtValue());
  } else if(targetType->getTypeID() == Type::PointerTyID) {
    return LONG2NUM(gv.IntVal.getZExtValue());
  }

  return LONG2NUM(-1);
}

VALUE
llvm_execution_engine_run_function_auto_args(int argc, VALUE *argv, VALUE klass) {
  if(argc < 1) { rb_raise(rb_eArgError, "Expected at least one argument - function name"); }
  CHECK_TYPE(argv[0], cLLVMFunction);
  Function *func = LLVM_FUNCTION(argv[0]);
  char errorMessage[128];

  const Function::ArgumentListType& nativeArguments(func->getArgumentList());

  if(argc - 1 != nativeArguments.size()){
    sprintf(errorMessage, "Function expects %zu arguments, but found: %d", nativeArguments.size(), argc - 1 );
    rb_raise(rb_eArgError, errorMessage);
  }

  std::vector<GenericValue> arg_values;
  int argvIndex = 1;

  for(Function::const_arg_iterator iter = nativeArguments.begin(); iter != nativeArguments.end(); iter++) {
    const Type * type = (*iter).getType();
    GenericValue arg;
    VALUE &rb_argument = argv[argvIndex];
    arg = Val2GV(rb_argument, type);
    arg_values.push_back(arg);
    argvIndex++;
  }

  GenericValue v = EE->runFunction(func, arg_values);

  const Type * retType = func->getReturnType();

  return Gv2Val(v, retType);
}

VALUE
llvm_execution_engine_run_function(int argc, VALUE *argv, VALUE klass) {
  if(argc < 1) { rb_raise(rb_eArgError, "Expected at least one argument"); }
  CHECK_TYPE(argv[0], cLLVMFunction);
  Function *func = LLVM_FUNCTION(argv[0]);

  // Using run function is much slower than getting C function pointer
  // and calling that, but it lets us pass in arbitrary numbers of
  // arguments easily for now, which is nice
  std::vector<GenericValue> arg_values;
  for(int i = 1; i < argc; ++i) {
    GenericValue arg_val;
    arg_val.IntVal = APInt(sizeof(long)*8, argv[i]);
    arg_values.push_back(arg_val);
  }

  GenericValue v = EE->runFunction(func, arg_values);
  VALUE val = v.IntVal.getZExtValue();
  return val;
}

/* For tests: assume no args, return uncoverted int and turn it into fixnum */
VALUE llvm_execution_engine_run_autoconvert(VALUE klass, VALUE func) {
  std::vector<GenericValue> args;
  GenericValue v = EE->runFunction(LLVM_FUNCTION(func), args);
  VALUE val = INT2NUM(v.IntVal.getZExtValue());
  return val;
}
}
