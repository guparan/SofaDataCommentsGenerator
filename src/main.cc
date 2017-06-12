#include <queue>
#include <chrono>

#include <clang/Basic/Diagnostic.h>
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


void encode(std::string& data) 
{
    std::string buffer;
    buffer.reserve(data.size());
    for(size_t pos = 0; pos != data.size(); ++pos) 
    {
        switch(data[pos]) 
        {
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

class FindInitCallVisitor : public RecursiveASTVisitor<FindInitCallVisitor> 
{
public:

    FindInitCallVisitor(Rewriter &R) : theRewriter(R) {}

    bool VisitCallExpr(CallExpr *e) 
    {
		if (member && e->getCalleeDecl() && e->getCalleeDecl()->getAsFunction()->getNameAsString() == "initData")
        {
			std::cout << member->getFirstDecl()->getLocation().printToString(theRewriter.getSourceMgr()) << " : " << member->getNameAsString();

			ASTContext &context = member->getFirstDecl()->getASTContext();
			const RawComment* rc = context.getRawCommentForDeclNoCache(member->getFirstDecl());
            if (rc) 
            {
                SourceRange range = rc->getSourceRange();

				PresumedLoc startPos = theRewriter.getSourceMgr().getPresumedLoc(range.getBegin());
				PresumedLoc endPos = theRewriter.getSourceMgr().getPresumedLoc(range.getEnd());

				std::string raw = rc->getRawText(theRewriter.getSourceMgr());
                std::cout << " : ALREADY COMMENTED :  " << raw;
            }
            else
            {
				//CallExpr::arg_iterator it = e->arg_end();
				SourceRange help_sr = e->getArg( e->getNumArgs() - 3 )->getSourceRange();
				
                // TODO: add comment to member here
				//context.addComment(RawComment(sr, RawComment::RCK_BCPLSlash, true, false, false));

				//PresumedLoc startPos = theRewriter.getSourceMgr().getPresumedLoc(sr.getBegin());


				//theRewriter.overwriteChangedFiles();


				//context.addComment(RawComment(context.getSourceManager(), member->getSourceRange(), true, false));
				theRewriter.InsertText(member->getFirstDecl()->getLocEnd(), " ///< "+theRewriter.getRewrittenText(help_sr));
				//theRewriter.overwriteChangedFiles();
				std::cout << " : " << theRewriter.getRewrittenText(help_sr);

				
				/*
				if (theRewriter.isRewritable(member->getFirstDecl()->getLocation()))
				{
					if (theRewriter.InsertTextAfter(member->getFirstDecl()->getLocation(), StringRef("THIS IS A TEST")))
					{
						std::cout << " : PROBLEM";
					}
					else
					{
						std::cout << " : OK";
					}

					std::cout << " : " << theRewriter.getRewrittenText(help_sr);
				}
				else
				{
					std::cout << " : NOT REWRITABLE";
				}
				

				
				std::cout << " : " << e->getNumArgs();
				for (int i = 0; i < e->getNumArgs(); ++i)
				{
					std::cout << " : " << theRewriter.getRewrittenText(e->getArg(i)->getSourceRange());
				}
				*/
				
			}

			//theRewriter.InsertText(member->getFirstDecl()->getLocation(), "THIS IS A TEST", true, true);
			//theRewriter.overwriteChangedFiles();

            std::cout << "\n";
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

        //std::cout << "Class " << decl->getQualifiedNameAsString() << " derives Base\n";
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





class ComponentHandler : public MatchFinder::MatchCallback
{
public:
	ComponentHandler(Rewriter &Rewrite) : Rewrite(Rewrite) {}

	virtual void run(const MatchFinder::MatchResult &Result) {
		// TODO
		//const VarDecl *IncVar = Result.Nodes.getNodeAs<VarDecl>("incVarName");
		//Rewrite.InsertText(IncVar->getLocStart(), "/* increment */", true, true);
	}

private:
	Rewriter &Rewrite;
};


class MyASTConsumer : public ASTConsumer 
{
public:
	MyASTConsumer(Rewriter &R) : Handler(R)
    {
        Matcher.addMatcher( 
            cxxRecordDecl( 
                isDerivedFrom(hasName("Base"))//,
                // has(cxxConstructorDecl(hasAnyConstructorInitializer(withInitializer(has(callExpr())))))
            ).bind("BaseDerived"),
			&Handler
        );
    }

    void HandleTranslationUnit(ASTContext &context) override 
    {
		Matcher.matchAST(context);
    }

private:
	ComponentHandler Handler;
    MatchFinder Matcher;
};

class MyFrontendAction : public ASTFrontendAction 
{
public:
    MyFrontendAction() {}
    
    void EndSourceFileAction() override 
    {
        //theRewriter.getEditBuffer( theRewriter.getSourceMgr().getMainFileID() ).write( llvm::outs() );

		// At this point the rewriter's buffer should be full with the rewritten
		// file contents.
		//const RewriteBuffer &RewriteBuf = theRewriter.getEditBuffer(theRewriter.getSourceMgr().getMainFileID());
		//RewriteBuf.write(llvm::outs());

		/*
		int test = RewriteBuf.size();
		std::cout << "############# SIZE : " << test << " ##############" << std::endl;
		for (RewriteRope::const_iterator it = RewriteBuf.begin(); it != RewriteBuf.end(); ++it)
		{
			std::cout << *it;
		}
		std::cout << std::endl;
		*/

		//std::cout << std::string(RewriteBuf.begin(), RewriteBuf.end());
		//llvm::outs() << std::string(RewriteBuf->begin(), RewriteBuf->end());

		/*
		const RewriteBuffer *RewriteBuf = theRewriter.getRewriteBufferFor(theRewriter.getSourceMgr().getMainFileID());
		if (RewriteBuf)
		{
			RewriteBuf->write(llvm::outs());
		}
		else
		{
			std::cout << "NO MODIF\n";
		}
		*/
    }

    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file) override 
    {
        theRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
        CI.getDiagnostics().setClient(new IgnoringDiagConsumer());
        std::cout<< "\n\n-------\nParsing file " << CI.getSourceManager().getFileEntryForID(theRewriter.getSourceMgr().getMainFileID())->getName() << "\n";
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

    nb_files = files.size();
    if (nb_files == 0) 
    {
        llvm::errs() << "No file founds" << "\n";
        exit(2);
    }

    clang::tooling::ClangTool tool(*compilation_database, files);
    tool.appendArgumentsAdjuster(
        [](const clang::tooling::CommandLineArguments &args, StringRef Filename) -> clang::tooling::CommandLineArguments {
            std::vector<std::string> a(args);
            //a.push_back("-isystem");
            a.push_back("C:/dev/llvm/install/lib/clang/4.0.0/include");
            a.push_back("-std=c++11");
            //a.push_back("-v");
            return a;
    });

    int r = tool.run(clang::tooling::newFrontendActionFactory<MyFrontendAction>().get());
	
    return r;
}
