#include <queue>
#include <chrono>

#include <clang/Basic/FileManager.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Tooling/Tooling.h>
#include <clang/Parse/ParseAST.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Lex/Lexer.h>
#include <iostream>

#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

static std::map<std::string, std::chrono::steady_clock::duration> ast_time;
static std::map<std::string, std::chrono::steady_clock::duration> visite_time;
static unsigned int current_file = 0;
static size_t nb_files = 0;

static clang::CompilerInstance *compiler_instance;

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::driver;
using namespace clang::tooling;

void encode(std::string& data) {
  std::string buffer;
  buffer.reserve(data.size());
  for(size_t pos = 0; pos != data.size(); ++pos) {
    switch(data[pos]) {
      case '&':  buffer.append("&amp;");       break;
      case '\"': buffer.append("&quot;");      break;
      case '\'': buffer.append("&apos;");      break;
      case '<':  buffer.append("&lt;");        break;
      case '>':  buffer.append("&gt;");        break;
      case '\n':  buffer.append("\\n");        break;
      case '\\':  buffer.append("\\\\");        break;
      case '|':  buffer.append("\\|");        break;
      default:   buffer.append(&data[pos], 1); break;
    }
  }
  data.swap(buffer);
}

class FindInitCallVisitor
  : public RecursiveASTVisitor<FindInitCallVisitor> {
public:

  FindInitCallVisitor(Rewriter &Rewrite) : Rewrite(Rewrite) {}

  bool VisitCallExpr(CallExpr *e) {

    if (e->getCalleeDecl() && member && e->getCalleeDecl()->getAsFunction()->getNameAsString() == "initData") {
      std::cout << member->getNameAsString()
                << "("
                << member->getFirstDecl()->getLocation().printToString(Rewrite.getSourceMgr())
                << ") : " << Rewrite.getRewrittenText(e->getArg(2)->getSourceRange()) << "\n";

      const RawComment* rc = member->getFirstDecl()->getASTContext().getRawCommentForDeclNoCache(member->getFirstDecl());
      if (rc) {
        SourceRange range = rc->getSourceRange();

        PresumedLoc startPos = Rewrite.getSourceMgr().getPresumedLoc(range.getBegin());
        PresumedLoc endPos = Rewrite.getSourceMgr().getPresumedLoc(range.getEnd());

        std::string raw = rc->getRawText(Rewrite.getSourceMgr());
        std::cout << " ---- already got a comment:  " << raw << "\n";
      }
    }

    return true;
  }

  FieldDecl *member;
  Rewriter &Rewrite;
};

class DerivedBaseHandler : public MatchFinder::MatchCallback {
 public:
  DerivedBaseHandler(Rewriter &Rewrite) : Rewrite(Rewrite), visitor(Rewrite) {}

  virtual void run(const MatchFinder::MatchResult &Result) {
    const CXXRecordDecl *decl = Result.Nodes.getNodeAs<CXXRecordDecl>("BaseDerived");

    if (!decl || !Rewrite.getSourceMgr().isInMainFile(decl->getLocation()))
      return;

    //std::cout << "Class " << decl->getQualifiedNameAsString() << " derives Base\n";
    for (const CXXConstructorDecl *ctor : decl->ctors()) {
      for (const CXXCtorInitializer *init : ctor->inits()) {
        if (init->isMemberInitializer()) {
          visitor.member = init->getMember();
          visitor.TraverseStmt(init->getInit());
        }
      }
    }
  }

 private:
  Rewriter &Rewrite;
  FindInitCallVisitor visitor;
};

class MyASTConsumer : public ASTConsumer {
 public:
    MyASTConsumer(Rewriter &R) : HandlerForDerivedBase(R) {
      Matcher.addMatcher(
          cxxRecordDecl(
              isDerivedFrom(hasName("Base"))/*,
              has(cxxConstructorDecl(hasAnyConstructorInitializer(
                  withInitializer(
                      has(callExpr())
                  )
              )))*/
          ).bind("BaseDerived"),

          &HandlerForDerivedBase);
    }

    void HandleTranslationUnit(ASTContext &Context) override {
      Matcher.matchAST(Context);
    }

 private:
    DerivedBaseHandler HandlerForDerivedBase;
    MatchFinder Matcher;
};

class MyFrontendAction : public ASTFrontendAction {
 public:
    MyFrontendAction() {}
    void EndSourceFileAction() override {
      /*TheRewriter.getEditBuffer(TheRewriter.getSourceMgr().getMainFileID())
          .write(llvm::outs());*/
    }

    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                   StringRef file) override {
      TheRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
//      std::cout<< "\n\n-------\nParsing file " << CI.getSourceManager().getFileEntryForID(TheRewriter.getSourceMgr().getMainFileID())->getName() << "\n";
      return llvm::make_unique<MyASTConsumer>(TheRewriter);
    }

 private:
    Rewriter TheRewriter;
};

/******************************************************************************/
/* main                                                                       */
/******************************************************************************/
int main(int argc, char **argv) {
  if (argc <= 1) {
    throw std::invalid_argument("No argument passed");
  }

  std::string error_message;
  std::string path (argv[1]);
  std::string ext = path.substr(path.find_last_of(".") + 1);
  std::unique_ptr< clang::tooling::CompilationDatabase > compilation_database;
  std::vector<std::string> files;

  if(ext == "cc" || ext == "c" || ext == "cxx" || ext == "cpp" || ext == "h" || ext == "hh") {
    files.push_back(path);
    int a = 3;
    const char * v[3] = {"--", "clang", path.c_str()};
    compilation_database = std::unique_ptr< clang::tooling::CompilationDatabase > ((clang::tooling::CompilationDatabase*) clang::tooling::FixedCompilationDatabase::loadFromCommandLine(
      a, v
    ));
  } else {
    compilation_database =
      clang::tooling::CompilationDatabase::autoDetectFromDirectory(
        path,
        error_message
      );
    if (!compilation_database) {
      llvm::errs() << "ERROR " << error_message << "\n";
      exit(1);
    }
    files = compilation_database->getAllFiles();
  }

  nb_files = files.size();
  if (nb_files == 0) {
    llvm::errs() << "No file founds" << "\n";
    exit(2);
  }

  clang::tooling::ClangTool tool(*compilation_database, files);
  tool.appendArgumentsAdjuster(
    [](const clang::tooling::CommandLineArguments &args, StringRef Filename) -> clang::tooling::CommandLineArguments {
      std::vector<std::string> a(args);
      a.push_back("-isystem");
      a.push_back("/home/jnbrunet/sources/llvm/release/lib/clang/4.0.0/include");
      a.push_back("-std=c++11");
//      a.push_back("-v");
      return a;
    }
  );

  int r = tool.run(clang::tooling::newFrontendActionFactory<MyFrontendAction>().get());

  return r;
}
