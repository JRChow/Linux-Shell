/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Author: Jingran Zhou                                               */
/*--------------------------------------------------------------------*/

#define _GNU_SOURCE /* For setenv and unsetenv to work properly. */

#include "lexAnalyzer.h"
#include "synAnalyzer.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <wait.h>
#include <fcntl.h>
#include <signal.h>

/*--------------------------------------------------------------------*/

/* The name of the executable binary file. */
static const char *pcPgmName;

/*--------------------------------------------------------------------*/

/* Return the value of the global variable pcPgmName, which means the
   name of the program. */

const char* getPgmName()
{
   assert(pcPgmName != NULL);

   return pcPgmName;
}

/*--------------------------------------------------------------------*/

static void handleRedirection(Command_T oCommand)
{
   /* The permissions of the newly-created file. */
   enum {PERMISSIONS = 0600};
   int iFd; /* File descriptor. */
   int iRet; /* Function return value. */
   
   /* Handle stdin-redirection. */
   if (Command_getStdIn(oCommand) != NULL)
   {
      iFd = open(Command_getStdIn(oCommand), O_RDONLY);
      if (iFd == -1) {perror(pcPgmName); exit(EXIT_FAILURE);}
      
      iRet = close(0); /* Close stdin. */
      if (iRet == -1) {perror(pcPgmName); exit(EXIT_FAILURE);}
      
      iRet = dup(iFd);
      if (iRet == -1) {perror(pcPgmName); exit(EXIT_FAILURE);}
      
      iRet = close(iFd);
      if (iRet == -1) {perror(pcPgmName); exit(EXIT_FAILURE);}
   }
   
   /* Handle stdout-redirection. */
   if (Command_getStdOut(oCommand) != NULL)
   {
      iFd = creat(Command_getStdOut(oCommand), PERMISSIONS);
      if (iFd == -1) {perror(pcPgmName); exit(EXIT_FAILURE);}

      iRet = close(1); /* Close stdout. */
      if (iRet == -1) {perror(pcPgmName); exit(EXIT_FAILURE);}

      iRet = dup(iFd);
      if (iRet == -1) {perror(pcPgmName); exit(EXIT_FAILURE);}

      iRet = close(iFd);
      if (iRet == -1) {perror(pcPgmName); exit(EXIT_FAILURE);}
   }
}

/*--------------------------------------------------------------------*/

/* Handle the cd command depending on ppcArgv and uArgsNum. */

static void handleCd(char** ppcArgv, size_t uArgsNum)
{
   /* If 0 argument, default to HOME. */
   if (uArgsNum == 0)
   {
      char* HOME;
      if ((HOME = getenv("HOME")) == NULL)
         fprintf(stderr, "%s: HOME is not set.\n", pcPgmName);
      else chdir(HOME);
   }
   /* Else if there are more than 1 argument, error. */
   else if (uArgsNum > 1)
      fprintf(stderr, "%s: too many arguments\n", pcPgmName);
   else /* Else call chdir normally. */
      chdir(ppcArgv[1]);
}

/*--------------------------------------------------------------------*/

/* Handle the unsetenv command depending on ppcArgv and uArgsNum. */

static void handleUnsetenv(char** ppcArgv, size_t uArgsNum)
{
   /* If the number of arguments is not 1, error. */
   if (uArgsNum != 1)
   {
      fprintf(stderr,
              "%s: missing variable\n", pcPgmName);
   }
   
   /* Else call unsetenv normally. */
   else unsetenv(ppcArgv[1]);
}

/*--------------------------------------------------------------------*/

/* Handle the setenv shell command with ppcArgv and uArgsNum. */

static void handleSetenv(char** ppcArgv, size_t uArgsNum)
{
   /* If there is 0 or more than 2 arguments, then error. */
   if (uArgsNum == 0 || uArgsNum > 2)
   {
      fprintf(stderr,
              "%s: missing variable\n", pcPgmName);
   }
   
   /* If only 1 argument, set to empty string. */
   else if (uArgsNum == 1) setenv(ppcArgv[1], "", 1);
   
   /* There are two commands, call setenv normally. */
   else setenv(ppcArgv[1], ppcArgv[2], 1);
}

/*--------------------------------------------------------------------*/

/* Execute oCommand. */
static void execCmd(Command_T oCommand, DynArray_T oTokens)
{
   size_t uArgsNum = Command_getArgsNum(oCommand);
   pid_t pid;
   int iRet; /* Function return value. */
   char** ppcArgv;

   assert(oCommand != NULL);
   
   /* Extract arguments into an array. */
   ppcArgv = Command_getArgv(oCommand);
   
   /* Handle exit shell command. */
   if (strcmp(Command_getName(oCommand), "exit") == 0)
   {
      if (oTokens != NULL)
      {
         LexAnalyzer_freeTokens(oTokens);
         DynArray_free(oTokens);
      }
      Command_free(oCommand);
      exit(0);
   }
   
   /* Handle setenv shell command. */
   else if (strcmp(Command_getName(oCommand), "setenv") == 0)
      handleSetenv(ppcArgv, uArgsNum);

   /* Handle unsetenv shell command. */
   else if (strcmp(Command_getName(oCommand), "unsetenv") == 0)
      handleUnsetenv(ppcArgv, uArgsNum);

   /* Handle cd shell command. */ 
   else if (strcmp(Command_getName(oCommand), "cd") == 0)
      handleCd(ppcArgv, uArgsNum);
   
   else /* Handle external commands. */
   {
      /* Flush buffer. */
      iRet = fflush(NULL);
      if (iRet == EOF) {perror(pcPgmName); exit(EXIT_FAILURE);}
      
      pid = fork();
      if (pid == -1) {perror(pcPgmName); exit(EXIT_FAILURE);}
      
      if (pid == 0)
      {
         /* Handle stdin and stdout file redirections. */
         handleRedirection(oCommand);

         /* In child, execute the command. */
         execvp(Command_getName(oCommand), ppcArgv);
         fprintf(stderr, "%s: No such file or directory\n", pcPgmName);
         exit(EXIT_FAILURE);
      }
      
      /* Parent waits. */
      pid = wait(NULL);
      if (pid == -1) {perror(pcPgmName); exit(EXIT_FAILURE);}
   }
   
   /* Free memory. */
   Command_free(oCommand);
   free(ppcArgv);
}

/*--------------------------------------------------------------------*/

int main(int argc, char* argv[])
{  
   char* pcLine;
   int iRet;
   DynArray_T oTokens = NULL;
   Command_T oCommand = NULL;
   
   pcPgmName = argv[0];

   /* Write to stdout a prompt. */
   printf("%% ");
   
   /* Read a line from stdin until reaching end-of-file. */
   while ((pcLine = LexAnalyzer_readLine(stdin)) != NULL)
   {
      /* Write the line to stdout and flush the buffer. */
      printf("%s\n", pcLine);
      iRet = fflush(stdout);
      if (iRet == EOF)
      {perror(pcPgmName); exit(EXIT_FAILURE);}
      
      /* Pass the line to the lexical analyzer. */
      oTokens = LexAnalyzer_lexLine(pcLine);
      free(pcLine);
      
      /* Pass the tokens to the syntactic analyzer. */
      if (oTokens == NULL) oCommand = NULL;
      else oCommand = SynAnalyzer_synTokens(oTokens);
      
      /* Execute the command, if any. */
      if (oCommand != NULL)
         execCmd(oCommand, oTokens);
      
      /* Free tokens. */
      if (oTokens != NULL)
      {
         LexAnalyzer_freeTokens(oTokens);
         DynArray_free(oTokens);
      }
      
      /* Write to stdout a prompt. */
      printf("%% ");
   } /* The while loop. */
   
   printf("\n");
   return 0;
}
