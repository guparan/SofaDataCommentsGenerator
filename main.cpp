#include <clang/Frontend/CompilerInstance.h>
#include <clang/Tooling/Tooling.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Lex/Lexer.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>

#include <iostream>
#include <string>
#include <regex>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::driver;
using namespace clang::tooling;


static std::list<std::string> parsed_members;
static int count = 0;


class FindInitCallVisitor : public RecursiveASTVisitor<FindInitCallVisitor>
{
public:

    FindInitCallVisitor(Rewriter &R) : theRewriter(R) {}

    void printUncommentedData(CallExpr *e, std::string memberLocation)
    {
        SourceRange help_sr = e->getArg(e->getNumArgs() - 3)->getSourceRange();
        std::string help_comment = theRewriter.getRewrittenText(help_sr);
        help_comment = help_comment.substr(1, help_comment.size() - 2); // remove first and last char (dublequotes)

        ASTContext &context = member->getFirstDecl()->getASTContext();
        const RawComment* rc = context.getRawCommentForDeclNoCache(member->getFirstDecl());

        if (!rc)
        {
            llvm::errs() << "NOT COMMENTED : " << memberLocation << " : " << member->getNameAsString();
            if (help_comment.length() == 0)
            {
                llvm::errs() << " : INITDATA EMPTY";
            }
            llvm::errs() << "\n";
            count++;
        }
    }

    void editDataComment(std::string comment)
    {
        ASTContext &context = member->getFirstDecl()->getASTContext();
        const RawComment* rc = context.getRawCommentForDeclNoCache(member->getFirstDecl());

        if (rc)
        {
            //llvm::errs() << "ALREADY COMMENTED :  " << std::string(rc->getRawText(theRewriter.getSourceMgr()));
            return;
        }

        // Old
        //theRewriter.InsertText(member->getFirstDecl()->getLocStart(), "/// " + help_comment + "\n", false, true);
        //theRewriter.InsertText(member->getFirstDecl()->getOuterLocStart() + member->getNameAsString().length(), " ///< " + theRewriter.getRewrittenText(help_sr), false);
        //theRewriter.ReplaceText(member->getFirstDecl()->getSourceRange(), "COUCOU");
        //RawComment newComment = RawComment(theRewriter.getSourceMgr(), member->getFirstDecl()->getSourceRange(), true, true);
        //context.addComment()

        int offset = 0;
        std::string declString = "";
        clang::SourceLocation begin(member->getFirstDecl()->getLocStart());
        clang::SourceLocation _e, end;

        while (declString.find(';') == std::string::npos) // searching end of declaration
        {
            _e = clang::SourceLocation(member->getFirstDecl()->getSourceRange().getEnd().getLocWithOffset(offset));
            end = clang::SourceLocation(clang::Lexer::getLocForEndOfToken(_e, 0, theRewriter.getSourceMgr(), theRewriter.getLangOpts()));
            declString = std::string(theRewriter.getSourceMgr().getCharacterData(begin), theRewriter.getSourceMgr().getCharacterData(end) - theRewriter.getSourceMgr().getCharacterData(begin));
            offset++;
        }

        //llvm::errs() << "\n QUALIFIER: " << member->getFirstDecl()->getSourceRange().getEnd().getLocWithOffset(1).printToString(theRewriter.getSourceMgr()) << "\n";
        //llvm::errs() << "\nSTART: " << member->getFirstDecl()->getLocStart().printToString(theRewriter.getSourceMgr()) <<
        //	           "\nEND: " << member->getFirstDecl()->getLocEnd().printToString(theRewriter.getSourceMgr()) << "\n";
        //llvm::errs() << "\nSTART: " << theRewriter.getSourceMgr().getPresumedLoc(member->getFirstDecl()->getLocStart()).getColumn() <<
        //			   "\nEND: " << theRewriter.getSourceMgr().getPresumedLoc(member->getFirstDecl()->getLocEnd()).getColumn() << "\n";
        //llvm::errs() << " : " << theRewriter.getRewrittenText(help_sr);
        //llvm::errs() << "\n";

        //llvm::errs() << end.printToString(theRewriter.getSourceMgr()) << " : " << comment << "\n";

        theRewriter.InsertText(end, " ///< " + comment);
        count++;
    }

    void editDataVariable(CallExpr *e)
    {
        std::regex validData("^(d_)(.*)");

        //llvm::errs() << "member->getDeclName().getAsString() = " << member->getDeclName().getAsString() << "\n";

        if (std::regex_match(member->getDeclName().getAsString(), validData))
        {

        }
    }


    bool VisitCallExpr(CallExpr *e)
    {
        //llvm::errs() << "VisitCallExpr\n";

        if (!member || !e->getCalleeDecl())
        {
            return true;
        }

        std::string memberLocation = member->getFirstDecl()->getLocation().printToString(theRewriter.getSourceMgr());
        if (std::find(parsed_members.begin(), parsed_members.end(), memberLocation) != parsed_members.end())
        {
            return true;
        }
        parsed_members.push_back(memberLocation);

        SourceRange comment_sr;
        if (e->getCalleeDecl()->getAsFunction()->getNameAsString() == "initData" && e->getNumArgs() >= 3)
        {
//            llvm::errs() << "initData : " << memberLocation << "\n";
            llvm::errs() << "initData found for member: " << member->getDeclName().getAsString() << "\n";
            comment_sr = e->getArg(e->getNumArgs() - 3)->getSourceRange();
        }
        /*else if (e->getCalleeDecl()->getAsFunction()->getNameAsString() == "initLink" && e->getNumArgs() >= 1)
        {
            //llvm::errs() << "initLink : " << memberLocation << "\n";
            comment_sr = e->getArg(e->getNumArgs() - 1)->getSourceRange();
        }*/
        else
        {
            return true;
        }

        std::string comment = theRewriter.getRewrittenText(comment_sr);
        comment = comment.substr(1, comment.size() - 2); // remove first and last char (doublequotes)

        if (comment.length() > 0)
        {
            llvm::errs() << "    comment: " << comment << "\n";
            editDataComment(comment);
        }

        return true;
    }

    FieldDecl *member;
    Rewriter &theRewriter;
};

class DerivedBaseHandler : public MatchFinder::MatchCallback
{
public:
    DerivedBaseHandler(Rewriter &R) : theRewriter(R), visitor(R) {}

    virtual void run(const MatchFinder::MatchResult &Result)
    {
        const CXXRecordDecl *decl = Result.Nodes.getNodeAs<CXXRecordDecl>("BaseDerived");

        if (!decl || !theRewriter.getSourceMgr().isInMainFile(decl->getLocation()))
        {
            return;
        }

        llvm::errs() << "Class " << decl->getQualifiedNameAsString() << " derives Base\n";
        for (const CXXConstructorDecl *ctor : decl->ctors())
        {
            for (const CXXCtorInitializer *init : ctor->inits())
            {
                if (init->isMemberInitializer())
                {
                    visitor.member = init->getMember();
                    visitor.TraverseStmt(init->getInit());
                }
            }
        }
    }

private:
    Rewriter &theRewriter;
    FindInitCallVisitor visitor;
};

class MyASTConsumer : public ASTConsumer
{
public:
    MyASTConsumer(Rewriter &R) : HandlerForDerivedBase(R)
    {
        Matcher.addMatcher(
            cxxRecordDecl(
                isDerivedFrom(hasName("Base"))//,
                // has(cxxConstructorDecl(hasAnyConstructorInitializer(withInitializer(has(callExpr())))))
            ).bind("BaseDerived"),
            &HandlerForDerivedBase
        );
    }

    void HandleTranslationUnit(ASTContext &context) override
    {
        Matcher.matchAST(context);
    }

private:
    DerivedBaseHandler HandlerForDerivedBase;
    MatchFinder Matcher;
};

class MyFrontendAction : public ASTFrontendAction
{
public:
    MyFrontendAction() {}

    void EndSourceFileAction() override
    {
        SourceManager &SM = theRewriter.getSourceMgr();
        llvm::errs() << "END Parsing file " << SM.getFileEntryForID(SM.getMainFileID())->getName() << "\n-------\n";

        theRewriter.overwriteChangedFiles();
        //theRewriter.getEditBuffer(theRewriter.getSourceMgr().getMainFileID()).write(llvm::outs());
    }

    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file) override
    {
        theRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
        CI.getDiagnostics().setClient(new IgnoringDiagConsumer());
        llvm::errs() << "\n-------\nBEGIN Parsing file " << CI.getSourceManager().getFileEntryForID(theRewriter.getSourceMgr().getMainFileID())->getName() << "\n";
        //llvm::errs() << std::flush;
        return llvm::make_unique<MyASTConsumer>(theRewriter);
    }

private:
    Rewriter theRewriter;
};


/******************************************************************************/
/* main                                                                       */
/******************************************************************************/
int main(int argc, char **argv) {
    if (argc <= 1)
    {
        throw std::invalid_argument("No argument passed");
    }

    std::string error_message;
    std::string path (argv[1]);
    std::string ext = path.substr(path.find_last_of(".") + 1);
    std::unique_ptr< clang::tooling::CompilationDatabase > compilation_database;
    std::vector<std::string> files;

    if(ext == "cc" || ext == "c" || ext == "cxx" || ext == "cpp" || ext == "h" || ext == "hh")
    {
        files.push_back(path);
        int a = 3;
        const char * v[3] = {"--", "clang", path.c_str()};
        compilation_database = std::unique_ptr< clang::tooling::CompilationDatabase > (
            (clang::tooling::CompilationDatabase*) clang::tooling::FixedCompilationDatabase::loadFromCommandLine(a, v)
        );
    }
    else
    {
        compilation_database = clang::tooling::CompilationDatabase::autoDetectFromDirectory(path,error_message);
        if (!compilation_database)
        {
            llvm::errs() << "ERROR " << error_message << "\n";
            exit(1);
        }
        files = compilation_database->getAllFiles();
    }

    if (files.size() == 0)
    {
        llvm::errs() << "No file founds" << "\n";
        exit(2);
    }

    clang::tooling::ClangTool tool(*compilation_database, files);
    tool.appendArgumentsAdjuster(
        [](const clang::tooling::CommandLineArguments &args, StringRef Filename) -> clang::tooling::CommandLineArguments {
            std::vector<std::string> a(args);
            //a.push_back("-isystem");
            //a.push_back("C:/dev/llvm/install/lib/clang/4.0.0/include");
            //a.push_back("-std=c++11");
            //a.push_back("-v");
            return a;
    });

    int r = tool.run(clang::tooling::newFrontendActionFactory<MyFrontendAction>().get());

    //llvm::errs() << "Done: " << count << " Data declarations edited.";

    return r;
}
