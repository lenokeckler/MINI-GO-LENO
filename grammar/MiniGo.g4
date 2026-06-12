grammar MiniGo;

// ============================================================
// PARSER RULES
// ============================================================

root
    : PACKAGE IDENTIFIER SEMI topDeclarationList EOF
    ;

topDeclarationList
    : ( variableDecl | typeDecl | funcDecl )*
    ;

// ---- Variable Declarations ----

variableDecl
    : VAR singleVarDecl SEMI
    | VAR LPAREN innerVarDecls RPAREN SEMI
    | VAR LPAREN RPAREN SEMI
    ;

innerVarDecls
    : ( singleVarDecl SEMI )+
    ;

singleVarDecl
    : identifierList declType ASSIGN expressionList    # varDeclTypedInit
    | identifierList ASSIGN expressionList             # varDeclInferred
    | singleVarDeclNoExps                              # varDeclNoInit
    ;

singleVarDeclNoExps
    : identifierList declType
    ;

// ---- Type Declarations ----

typeDecl
    : TYPE singleTypeDecl SEMI
    | TYPE LPAREN innerTypeDecls RPAREN SEMI
    | TYPE LPAREN RPAREN SEMI
    ;

innerTypeDecls
    : ( singleTypeDecl SEMI )+
    ;

singleTypeDecl
    : IDENTIFIER declType
    ;

// ---- Function Declarations ----

funcDecl
    : funcFrontDecl block SEMI
    ;

funcFrontDecl
    : FUNC IDENTIFIER LPAREN funcArgDecls? RPAREN declType?
    ;

funcArgDecls
    : singleVarDeclNoExps ( COMMA singleVarDeclNoExps )*
    ;

// ---- Type Expressions ----

declType
    : LPAREN declType RPAREN                          # parenDeclType
    | IDENTIFIER                                      # namedDeclType
    | LBRACK RBRACK declType                          # sliceDeclType
    | LBRACK INTLITERAL RBRACK declType               # arrayDeclType
    | STRUCT LBRACE structMemDecls? RBRACE            # structDeclType
    ;

structMemDecls
    : ( singleVarDeclNoExps SEMI )+
    ;

identifierList
    : IDENTIFIER ( COMMA IDENTIFIER )*
    ;

// ---- Expressions (ANTLR4 precedence climbing — higher listed = higher precedence) ----

expression
    : primaryExpression                                                              # primaryExpr
    | op=( PLUS | MINUS | NOT | XOR ) expression                                   # unaryExpr
    | expression op=( STAR | SLASH | PERCENT | SHL | SHR | AMP | AMP_CARET ) expression  # mulExpr
    | expression op=( PLUS | MINUS | PIPE | XOR ) expression                       # addExpr
    | expression op=( EQ | NEQ | LT | LEQ | GT | GEQ ) expression                 # relExpr
    | expression op=AND_AND expression                                              # andExpr
    | expression op=OR_OR  expression                                               # orExpr
    ;

expressionList
    : expression ( COMMA expression )*
    ;

// ---- Primary Expressions ----

primaryExpression
    : operand                                             # operandExpr
    | primaryExpression DOT IDENTIFIER                    # selectorExpr
    | primaryExpression LBRACK expression RBRACK          # indexExpr
    | primaryExpression LPAREN expressionList? RPAREN     # callExpr
    | APPEND LPAREN expression COMMA expression RPAREN    # appendExpr
    | LEN    LPAREN expression RPAREN                     # lenExpr
    | CAP    LPAREN expression RPAREN                     # capExpr
    ;

operand
    : literal                             # literalOperand
    | IDENTIFIER                          # identifierOperand
    | LPAREN expression RPAREN            # parenOperand
    ;

literal
    : INTLITERAL              # intLit
    | FLOATLITERAL            # floatLit
    | RUNELITERAL             # runeLit
    | RAWSTRINGLITERAL        # rawStringLit
    | INTERPRETEDSTRINGLITERAL # interpretedStringLit
    ;

// ---- Statements ----

statementList
    : statement*
    ;

block
    : LBRACE statementList RBRACE
    ;

statement
    : PRINT   LPAREN expressionList? RPAREN SEMI     # printStmt
    | PRINTLN LPAREN expressionList? RPAREN SEMI     # printlnStmt
    | RETURN expression? SEMI                         # returnStmt
    | BREAK    SEMI                                   # breakStmt
    | CONTINUE SEMI                                   # continueStmt
    | simpleStatement SEMI                            # simpleStmt
    | block SEMI                                      # blockStmt
    | switchStatement SEMI                            # switchStmt
    | ifStatement SEMI                                # ifStmt
    | loop SEMI                                       # loopStmt
    | typeDecl                                        # typeDeclStmt
    | variableDecl                                    # varDeclStmt
    ;

simpleStatement
    : expressionList DECLARE_ASSIGN expressionList    # shortVarDeclStmt
    | expressionList ASSIGN         expressionList    # assignStmt
    | expression ADD_ASSIGN     expression            # addAssignStmt
    | expression SUB_ASSIGN     expression            # subAssignStmt
    | expression MUL_ASSIGN     expression            # mulAssignStmt
    | expression DIV_ASSIGN     expression            # divAssignStmt
    | expression MOD_ASSIGN     expression            # modAssignStmt
    | expression SHL_ASSIGN     expression            # shlAssignStmt
    | expression SHR_ASSIGN     expression            # shrAssignStmt
    | expression AND_ASSIGN     expression            # andAssignStmt
    | expression OR_ASSIGN      expression            # orAssignStmt
    | expression XOR_ASSIGN     expression            # xorAssignStmt
    | expression AND_NOT_ASSIGN expression            # andNotAssignStmt
    | expression INC                                  # incStmt
    | expression DEC                                  # decStmt
    | expression                                      # exprStmt
    |                                                 # emptyStmt
    ;

ifStatement
    : IF simpleStatement SEMI expression block ELSE ifStatement   # ifWithInitElseIf
    | IF simpleStatement SEMI expression block ELSE block         # ifWithInitElse
    | IF simpleStatement SEMI expression block                    # ifWithInit
    | IF expression block ELSE ifStatement                        # ifElseIf
    | IF expression block ELSE block                              # ifElse
    | IF expression block                                         # ifSimple
    ;

loop
    : FOR block                                                            # infiniteLoop
    | FOR expression block                                                 # condLoop
    | FOR simpleStatement SEMI expression SEMI simpleStatement block       # forLoop
    | FOR simpleStatement SEMI SEMI simpleStatement block                  # forLoopNoExpr
    ;

switchStatement
    : SWITCH simpleStatement SEMI expression LBRACE expressionCaseClauseList RBRACE  # switchWithInit
    | SWITCH expression            LBRACE expressionCaseClauseList RBRACE            # switchSimple
    | SWITCH simpleStatement SEMI  LBRACE expressionCaseClauseList RBRACE            # switchInitNoExpr
    | SWITCH                       LBRACE expressionCaseClauseList RBRACE            # switchNoExpr
    ;

expressionCaseClauseList
    : expressionCaseClause*
    ;

expressionCaseClause
    : expressionSwitchCase COLON statementList
    ;

expressionSwitchCase
    : CASE expressionList  # caseClause
    | DEFAULT              # defaultClause
    ;

// ============================================================
// LEXER RULES
// ============================================================

// ---- Keywords (must appear before IDENTIFIER) ----
PACKAGE  : 'package'  ;
VAR      : 'var'      ;
TYPE     : 'type'     ;
FUNC     : 'func'     ;
STRUCT   : 'struct'   ;
IF       : 'if'       ;
ELSE     : 'else'     ;
FOR      : 'for'      ;
SWITCH   : 'switch'   ;
CASE     : 'case'     ;
DEFAULT  : 'default'  ;
RETURN   : 'return'   ;
BREAK    : 'break'    ;
CONTINUE : 'continue' ;
APPEND   : 'append'   ;
LEN      : 'len'      ;
CAP      : 'cap'      ;
PRINT    : 'print'    ;
PRINTLN  : 'println'  ;

// ---- Multi-character operators (longest match first) ----
DECLARE_ASSIGN  : ':='  ;
SHL_ASSIGN      : '<<=' ;
SHR_ASSIGN      : '>>=' ;
AND_NOT_ASSIGN  : '&^=' ;
AND_ASSIGN      : '&='  ;
OR_ASSIGN       : '|='  ;
XOR_ASSIGN      : '^='  ;
ADD_ASSIGN      : '+='  ;
SUB_ASSIGN      : '-='  ;
MUL_ASSIGN      : '*='  ;
DIV_ASSIGN      : '/='  ;
MOD_ASSIGN      : '%='  ;
AND_AND         : '&&'  ;
OR_OR           : '||'  ;
EQ              : '=='  ;
NEQ             : '!='  ;
LEQ             : '<='  ;
GEQ             : '>='  ;
SHL             : '<<'  ;
SHR             : '>>'  ;
AMP_CARET       : '&^'  ;
INC             : '++'  ;
DEC             : '--'  ;

// ---- Single-character operators and punctuation ----
ASSIGN  : '=' ;
PLUS    : '+' ;
MINUS   : '-' ;
STAR    : '*' ;
SLASH   : '/' ;
PERCENT : '%' ;
AMP     : '&' ;
PIPE    : '|' ;
XOR     : '^' ;
NOT     : '!' ;
LT      : '<' ;
GT      : '>' ;
LPAREN  : '(' ;
RPAREN  : ')' ;
LBRACE  : '{' ;
RBRACE  : '}' ;
LBRACK  : '[' ;
RBRACK  : ']' ;
SEMI    : ';' ;
COMMA   : ',' ;
DOT     : '.' ;
COLON   : ':' ;

// ---- Literals ----

INTLITERAL
    : '0' ( [xX] [0-9a-fA-F]+ | [oO] [0-7]+ | [bB] [01]+ | [0-7]* )
    | [1-9] [0-9]*
    ;

FLOATLITERAL
    : [0-9]+ '.' [0-9]* ( [eE] [+\-]? [0-9]+ )?
    | '.'     [0-9]+    ( [eE] [+\-]? [0-9]+ )?
    | [0-9]+              [eE] [+\-]? [0-9]+
    ;

RUNELITERAL
    : '\'' ( ~['\\\n\r] | '\\' . ) '\''
    ;

RAWSTRINGLITERAL
    : '`' ~'`'* '`'
    ;

INTERPRETEDSTRINGLITERAL
    : '"' ( ~["\\\n\r] | '\\' . )* '"'
    ;

IDENTIFIER
    : [a-zA-Z_] [a-zA-Z0-9_]*
    ;

// ---- Skipped tokens ----
LINE_COMMENT  : '//' ~[\r\n]*  -> skip ;
BLOCK_COMMENT : '/*' .*? '*/'  -> skip ;
WS            : [ \t\r\n]+     -> skip ;
