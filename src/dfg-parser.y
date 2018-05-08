%code requires {
  #include <stdint.h>
  #include "sbpdg.h"
  int parse_dfg(const char* dfgfile, SbPDG* dfg);
  typedef std::pair<std::string,int> io_pair_t;
}

%{
#include "dfg-parser.tab.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <map>
extern int yylineno;
void yyrestart(FILE *); 

struct parse_param {
  SymTab syms;
  SbPDG* dfg;
};

int yylex();     
static void yyerror(parse_param*, const char *); 
%}

%parse-param {struct parse_param* p}

%token	 INPUT OUTPUT EOLN NEW_DFG
%token<s> IDENT STRING_LITERAL 
%token<d> F_CONST
%token<i> I_CONST


%union {
        std::string* s;
        uint64_t i;
	double d;	 
        io_pair_t* io_pair;
 	SbPDG_Node* node;
	std::vector<SymEntry>* sym_vec;
	SymEntry sym_ent;
        YYSTYPE() {}   // this is only okay because sym_ent doesn't need a
        ~YYSTYPE() {}  // real constructor/deconstructuor (but string/vector do)
}


%type <io_pair> io_def
%type <sym_vec> arg_list  
%type <sym_ent> arg_expr  rhs expr

%start statement_list
%debug
%error-verbose

%%

statement_list
	: statement 
	| statement_list statement
	;

statement
	: INPUT ':' io_def  eol {
          if($3->second==0) p->dfg->addScalarInput($3->first.c_str(),p->syms);
          else p->dfg->addVecInput($3->first.c_str(),$3->second,p->syms);
          //printf("input  %s, %d\n",$3->first.c_str(),$3->second);
          delete $3;
          }
	| OUTPUT ':' io_def eol {
          if($3->second==0) p->dfg->addScalarOutput($3->first.c_str(),p->syms);
          else p->dfg->addVecOutput($3->first.c_str(),$3->second,p->syms);
          //printf("output %s, %d\n",$3->first.c_str(),$3->second);
          delete $3;
          }
	| IDENT '=' rhs eol {
             SymEntry& s = $3;
             if(s.type == SymEntry::SYM_NODE) {
               SbPDG_Node* n = s.data.node; 
               n->setName(*$1);
               //printf("%s assigned to instruction\n",$1->c_str());
             } 
             //else printf("%s assigned to constant\n",$1->c_str());
             p->syms.set(*$1,s); 
             delete $1;
           }
	| eol
        | NEW_DFG eol { p->dfg->start_new_dfg_group();}
	;

eol     : ';'
	| EOLN
	;

io_def
	: IDENT                  {$$ = new io_pair_t(*$1,0); delete $1;}
	| IDENT '[' I_CONST ']'  {$$ = new io_pair_t(*$1,$3); delete $1;}
	;

rhs     : expr {$$ = $1;}
	;


expr    : I_CONST {$$ = SymEntry($1);}
        | I_CONST I_CONST {$$ = SymEntry($1,$2);}
        | I_CONST I_CONST I_CONST I_CONST {$$ = SymEntry($1,$2,$3,$4);}
	| F_CONST {$$ = SymEntry($1);}
        | F_CONST F_CONST {$$ = SymEntry($1,$2);}
	| IDENT                  {$$ = p->syms.get_sym(*$1); delete $1;}
	| IDENT '(' arg_list ')' {$$ = p->dfg->createInst(*$1,*$3); 
                                  delete $1; delete $3;}
arg_expr 
	: expr {$$ = $1;}
	| IDENT '=' expr { $$ = $3; $$.set_flag(*$1); }
	;

arg_list : arg_expr {$$ = new std::vector<SymEntry>(); 
	             $$->push_back($1);} 
	 | arg_list ',' arg_expr {$1->push_back($3); 
                                  $$ = $1;}
	 ;

%%



int parse_dfg(const char* filename, SbPDG* _dfg) {
  FILE* dfg_file = fopen(filename, "r");
  if (dfg_file == NULL) {
      perror(" Failed ");
      exit(1);
  }

  struct parse_param p;
  p.dfg = _dfg;

  yyrestart(dfg_file);
  yyparse(&p);
  return 0;
}


static void yyerror(struct parse_param* p, char const* s)
{
  fprintf(stderr, "Error parsing DFG at line %d: %s\n", yylineno, s);
  assert(0);
}



/*		| expression {
			yylval.fun->dub = $1; 
			printf("%lf\n", yylval.fun->dub);
		}
*/

