%{
#include "siparse.h"
#include "siparseutils.h"
#include <stdio.h>
    
extern int yyleng;
int yylex(void);
void yyerror(char *);
  
void switchinputbuftostring(const char *);
void freestringinputbuf(void);

static line parsed_line;
%}


%union {
	int flags;
	char *name;
	char **argv;
	redirection *redir;
	redirection **redirseq;
	command *comm;
	pipeline pipeln;
	pipelineseq pipelnsq;
	line* parsedln;
}

%token SSTRING
%token OAPPREDIR
%%
line: pipelineseq mamp mendl {
			parsed_line.pipelines = closepipelineseq(); 
			parsed_line.flags = $<flags>2;
			$<parsedln>$ = &parsed_line;
		}
	;

mamp: 
	'&' { $<flags>$ = LINBACKGROUND; } 
	|	{ $<flags>$ = 0; }
	;

mendl:
	'\n'
	|
	;

pipelineseq:
	pipelineseq ';' prepipeline{
			$<pipelnsq>$ = appendtopipelineseq($<pipeln>3);
		}
	| prepipeline{
			$<pipelnsq>$ = appendtopipelineseq($<pipeln>1);
		}
	;

prepipeline:
	pipeline {
			closepipeline();
		}
	;

pipeline:
	pipeline '|' single {
			$<pipeln>$ = appendtopipeline($<comm>3);
		}
	| single {
			$<pipeln>$ = appendtopipeline($<comm>1);
		}
	;

single:
	allnames allredirs {
			if ($<argv>1==NULL) {
				$<comm>$ = NULL;	
			} else {
				command *com= nextcommand();
				com->argv = $<argv>1;
				com->redirs = $<redirseq>2;
				$<comm>$ = com;
			}
		}
	;

allnames:
		names { $<argv>$ = closeargv(); }


allredirs:
		 redirs { $<redirseq>$ = closeredirseq(); }

names:
	names name {
			$<argv>$ = appendtoargv($<name>2);
		} 
	|	 
	;

name:	SSTRING {
			$<name>$ = copytobuffer(yyval.name, yyleng+1);
		};

redirs:
	redirs redir {
			$<redirseq>$ = appendtoredirseq($<redir>2);
		}
	|	{	$<redirseq>$ = NULL; };
	;

redir:
	redirIn
	| redirOut
	;

redirIn:
	'<' rname { $<redir>2->flags = RIN; $<redir>$=$<redir>2; }
	;

redirOut:
	OAPPREDIR rname 	{ $<redir>2->flags = ROUT | RAPPEND ; $<redir>$=$<redir>2; }
	| '>' rname	{ $<redir>2->flags = ROUT; $<redir>$=$<redir>2; }
	;

rname:
	 name {
			redirection * red;

			red=nextredir();
			red->filename = $<name>1;
			$<redir>$= red;
		}

%%

void yyerror(char *s) {
}


line * parseline(char *str){
	int parseresult;

	resetutils();
	switchinputbuftostring(str);
	parseresult = yyparse();
	freestringinputbuf();

	if (parseresult) return NULL;
	return &parsed_line;
}

