/*==============================================================================
MIT License

Copyright (c) 2023 Trevor Monk

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
==============================================================================*/

/*!
 * @defgroup fcgi_proc fcgi_proc
 * @brief Fast CGI Interface for process management
 * @{
 */

/*============================================================================*/
/*!
@file fcgi_proc.c

    FCGI Process Management

    The fcgi_proc Application provides a Fast CGI interface to support
    process management using the procmon CLI application.
    It can be interfaced via a web server such as lighttpd.

*/
/*============================================================================*/

/*==============================================================================
        Includes
==============================================================================*/

#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcgi_stdio.h>

/*==============================================================================
        Private definitions
==============================================================================*/

/*! Maximum POST content length */
#define MAX_POST_LENGTH         1024L

/*! FCGIProc state */
typedef struct _FCGIProcState
{
    /*! maximum POST data length */
    size_t maxPostLength;

    /*! POST buffer */
    char *postBuffer;

    /*! verbose flag */
    bool verbose;

} FCGIProcState;

/*! query processing functions */
typedef struct _queryFunc
{
    /*! query tag string to associate with a tag processing function */
    char *tag;

    /*! pointer to the function to handle the tag data */
    int (*pTagFn)(FCGIProcState *, char *);

} QueryFunc;

/*! Handler function */
typedef int (*HandlerFunction)(FCGIProcState *);

/*! FCGI Handler function */
typedef struct _fcgi_handler
{
    /*! handler name */
    char *handler;

    /*! handler function */
    HandlerFunction fn;
} FCGIHandler;

#ifndef EOK
#define EOK (0)
#endif

/*==============================================================================
        Private function declarations
==============================================================================*/

void main(int argc, char **argv);
static int InitState( FCGIProcState *pState );
static int ProcessOptions( int argC, char *argV[], FCGIProcState *pState );
static void usage( char *cmdname );
static int ProcessRequests( FCGIProcState *pState,
                            FCGIHandler *pFCGIHandlers,
                            size_t numHandlers );

static int ProcessGETRequest( FCGIProcState *pState );
static int ProcessPOSTRequest( FCGIProcState *pState );
static int GetPOSTData( FCGIProcState *pState, size_t length );
static int ProcessUnsupportedRequest( FCGIProcState *pState );
static int ProcessQuery( FCGIProcState *pState, char *request );

static int ProcessQueryFunctions( FCGIProcState *pState,
                                  char *query,
                                  QueryFunc *pFns,
                                  int numFuncs );

static int InvokeQueryFunction( FCGIProcState *pState,
                                char *query,
                                QueryFunc *pFns,
                                int numFuncs );

static int ValidateProcName( char *procname );

static int ExecuteCommand( char *cmd, bool json );

static int ProcessStartRequest( FCGIProcState *pState, char *query );
static int ProcessStopRequest( FCGIProcState *pState, char *query );
static int ProcessRestartRequest( FCGIProcState *pState, char *query );
static int ProcessListRequest( FCGIProcState *pState, char *query );

static int AllocatePOSTBuffer( FCGIProcState *pState );
static int ClearPOSTBuffer( FCGIProcState *pState );
static void SetupTerminationHandler( void );
static void TerminationHandler( int signum, siginfo_t *info, void *ptr );

static HandlerFunction GetHandlerFunction( char *method,
                                           FCGIHandler *pFCGIHandlers,
                                           size_t numHandlers );

static void SendHeader( void );
static void SendJSONHeader( void );
static int ErrorResponse( int status,  char *description );

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! array of HTTP method handlers */
FCGIHandler methodHandlers[] =
{
    { "GET", ProcessGETRequest },
    { "POST", ProcessPOSTRequest },
    { "*", ProcessUnsupportedRequest }
};

/* FCGI Vars State object */
FCGIProcState state;

/*==============================================================================
        Private function definitions
==============================================================================*/

/*============================================================================*/
/*  main                                                                      */
/*!
    Main entry point for the fcgi_proc application

    The main function starts the fcgi_proc application

    @param[in]
        argc
            number of arguments on the command line
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @return none

==============================================================================*/
void main(int argc, char **argv)
{
    /* initialize the FCGI Vars state */
    InitState( &state );

    /* set up the termination handler */
    SetupTerminationHandler();

    /* process the command line options */
    ProcessOptions( argc, argv, &state );

    /* allocate memory for the POST data buffer */
    if( AllocatePOSTBuffer( &state ) == EOK )
    {
        /* process FCGI requests */
        ProcessRequests( &state,
                            methodHandlers,
                            sizeof(methodHandlers) / sizeof(FCGIHandler) );
    }
    else
    {
        syslog( LOG_ERR, "Cannot allocate POST buffer" );
    }
}

/*============================================================================*/
/*  InitState                                                                 */
/*!
    Initialize the FCGIProc state

    The InitState function initializes the FCGIProc state object

    @param[in]
        pState
            pointer to the FCGIProcState object to initialize

    @retval EOK the FCGIProcState object was successfully initialized
    @retval EINVAL invalid arguments

==============================================================================*/
static int InitState( FCGIProcState *pState )
{
    int result = EINVAL;

    if ( pState != NULL )
    {
        /* clear the state */
        memset( pState, 0, sizeof( FCGIProcState ) );

        /* set the default POST content length */
        pState->maxPostLength = MAX_POST_LENGTH;

        result = EOK;
    }

    return result;
}

/*============================================================================*/
/*  usage                                                                     */
/*!
    Display the application usage

    The usage function dumps the application usage message
    to stderr.

    @param[in]
       cmdname
            pointer to the invoked command name

    @return none

==============================================================================*/
static void usage( char *cmdname )
{
    if( cmdname != NULL )
    {
        fprintf(stderr,
                "usage: %s [-v] [-h] "
                " [-h] : display this help"
                " [-v] : verbose output"
                " [-l <max POST length>] : maximum POST data length",
                cmdname );
    }
}

/*============================================================================*/
/*  ProcessOptions                                                            */
/*!
    Process the command line options

    The ProcessOptions function processes the command line options and
    populates the FCGIProcState object

    @param[in]
        argC
            number of arguments
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @param[in]
        pState
            pointer to the FCGIProc state object

    @retval EOK options processed successfully
    @retval ENOTSUP unsupported option
    @retval EINVAL invalid arguments

==============================================================================*/
static int ProcessOptions( int argC, char *argV[], FCGIProcState *pState )
{
    int c;
    int result = EINVAL;
    const char *options = "hvl:";

    if( ( pState != NULL ) &&
        ( argV != NULL ) )
    {
        result = EOK;

        while( ( c = getopt( argC, argV, options ) ) != -1 )
        {
            switch( c )
            {
                case 'v':
                    pState->verbose = true;
                    break;

                case 'l':
                    pState->maxPostLength = strtoul( optarg, NULL, 0 );
                    break;

                case 'h':
                    usage( argV[0] );
                    break;

                default:
                    result = ENOTSUP;
                    break;

            }
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessRequests                                                           */
/*!
    Process incoming Fast CGI requests

    The ProcessRequests function waits for incoming FCGI requests
    and processes them according to their request method.
    Typically this function will not exit, as doing so will terminate
    the FCGI interface.

    @param[in]
        pState
            pointer to the FCGIProc state object

    @param[in]
        pFCGIHandlers
            pointer to an array of FCGIHandler objects which link method
            names (eg GET, POST) with their method handling functions.

    @param[in]
        numHandlers
            number of handlers in the array of FCGIHandler objects

    @retval EOK request processed successfully
    @retval EINVAL invalid arguments

==============================================================================*/
static int ProcessRequests( FCGIProcState *pState,
                            FCGIHandler *pFCGIHandlers,
                            size_t numHandlers )
{
    int result = EINVAL;
    char *method;
    HandlerFunction fn = NULL;

    if ( ( pState != NULL ) &&
         ( pFCGIHandlers != NULL ) &&
         ( numHandlers > 0 ) )
    {
        /* wait for an FCGI request */
        while( FCGI_Accept() >= 0 )
        {
            /* check the request method */
            method = getenv("REQUEST_METHOD");
            if ( method != NULL )
            {
                /* get the handler associated with the method */
                fn = GetHandlerFunction( method, pFCGIHandlers, numHandlers );
                if ( fn != NULL )
                {
                    /* invoke the handler */
                    result = fn( pState );
                }
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  GetHandlerFunction                                                        */
/*!
    Get the handler function for the specified method

    The GetHandlerFunction function looks up the processing function
    associated with the specified HTTP method.

    The handler functions are passed in via the pFCGIHandler pointer

    @param[in]
        method
            pointer to the method name, eg "GET", "POST"

    @param[in]
        pFCGIHandlers
            pointer to the FCGI method handling functions

    @param[in]
        numHandlers
            number of handlers in the method handling function array pointed
            to by pFCGIHandler

    @retval pointer to the method handler
    @retval NULL no method handler could be found

==============================================================================*/
static HandlerFunction GetHandlerFunction( char *method,
                                           FCGIHandler *pFCGIHandlers,
                                           size_t numHandlers )
{
    size_t i;
    FCGIHandler *pFCGIHandler;
    HandlerFunction fn = NULL;

    if ( ( method != NULL ) &&
         ( pFCGIHandlers != NULL ) &&
         ( numHandlers > 0 ) )
    {
        /* iterate through the FCGI method handlers */
        for ( i = 0; i < numHandlers ; i++ )
        {
            /* get a pointer to the current method handler */
            pFCGIHandler = &pFCGIHandlers[i];
            if ( pFCGIHandler != NULL )
            {
                /* check if it matches the REQUEST_METHOD or the
                 * wild card */
                if ( ( strcmp( pFCGIHandler->handler, method ) == 0 ) ||
                     ( strcmp( pFCGIHandler->handler, "*" ) == 0 ) )
                {
                    /* get a pointer to the handler function */
                    fn = pFCGIHandler->fn;
                    break;
                }
            }
        }
    }

    return fn;
}

/*============================================================================*/
/*  ProcessGETRequest                                                         */
/*!
    Process a Fast CGI GET request

    The ProcessGETRequest function processes a single FCGI GET request
    contained in the QUERY_STRING environment variable

    @param[in]
        pState
            pointer to the FCGIProc state object

    @retval EOK request processed successfully
    @retval EINVAL invalid arguments

==============================================================================*/
static int ProcessGETRequest( FCGIProcState *pState )
{
    int result = EINVAL;
    char *query;

    if ( pState != NULL )
    {
        /* get the query string */
        query = getenv("QUERY_STRING");

        /* process the request */
        result = ProcessQuery( pState, query );
    }
	else
	{
	    result = ErrorResponse( 400, "Bad request" );
	}

    return result;
}

/*============================================================================*/
/*  ProcessPOSTRequest                                                        */
/*!
    Process a Fast CGI POST request

    The ProcessPOSTRequest function processes a single FCGI POST request
    where the request is contained in the body of the message

    @param[in]
        pState
            pointer to the FCGIProc state object

    @retval EOK request processed successfully
    @retval EINVAL invalid arguments

==============================================================================*/
static int ProcessPOSTRequest( FCGIProcState *pState )
{
    int result = EINVAL;
    char *contentLength;
    size_t length;

    if ( pState != NULL )
    {
        /* get the content length */
        contentLength = getenv("CONTENT_LENGTH");
        if( contentLength != NULL )
        {
            /* convert the content length to an integer */
            length = strtoul(contentLength, NULL, 0);
            if ( ( length > 0 ) && ( length <= pState->maxPostLength ) )
            {
                /* read the query from the POST Data */
                result = GetPOSTData( pState, length );
                if( result == EOK )
                {
                    /* Process the request */
                    result = ProcessQuery( pState, pState->postBuffer );

                    /* clear the POST buffer.  This is critical since
                     * the buffer must be zeroed before the next read in order
                     * to make sure it is correctly NUL terminated */
                    ClearPOSTBuffer( pState );
                }
            }
            else
            {
                /* content length is too large (or too small) */
                ErrorResponse( 413, "Invalid Content-Length" );
            }
        }
        else
        {
            /* unable to get content length */
            ErrorResponse( 413, "Invalid Content-Length" );
        }
    }

    return result;
}

/*============================================================================*/
/*  GetPOSTData                                                               */
/*!
    Read the POST data from a Fast CGI POST request

    The GetPOSTData function reads the POST data into the POST data
    buffer in the FCGIProcState object.  It is assumed that the
    content length has already been determined and is specified
    in the length parameter.

    Note that this function does NOT NUL terminate the input buffer.
    This buffer is assumed to be zeroed before each read

    @param[in]
        pState
            pointer to the FCGIProc state object

    @param[in]
        length
            content-length bytes to read

    @retval EOK request processed successfully
    @retval ENXIO I/O error
    @retval ENOMEM not enough memory to read the POST data
    @retval EINVAL invalid arguments

==============================================================================*/
static int GetPOSTData( FCGIProcState *pState, size_t length )
{
    int result = EINVAL;

    if ( pState != NULL )
    {
        if( length <= pState->maxPostLength )
        {
            /* read content-length bytes of data */
            if ( FCGI_fread( pState->postBuffer, length, 1, FCGI_stdin ) == 1 )
            {
                /* content-length bytes of data successfully read */
                result = EOK;
            }
            else
            {
                /* unable to read content-length bytes of data */
                result = ENXIO;
            }
        }
        else
        {
            /* not enough memory to read content-length bytes of data */
            result = ENOMEM;
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessUnsupportedRequest                                                 */
/*!
    Process a Fast CGI request using an unsupport request method

    The ProcessUnsupportedRequest function processes a single FCGI request
    where the request method is not supported

    @param[in]
        pState
            pointer to the FCGIProc state object

    @retval EOK request processed successfully
    @retval EINVAL invalid arguments

==============================================================================*/
static int ProcessUnsupportedRequest( FCGIProcState *pState )
{
    int result = EINVAL;

    if ( pState != NULL )
    {
        result = ErrorResponse( 405, "Method Not Allowed" );
    }

    return result;
}

/*============================================================================*/
/*  ProcessQuery                                                              */
/*!
    Process a Variable Query

    The ProcessQuery function processes a single variable query

    @param[in]
        pState
            pointer to the FCGIProc state object

    @param[in]
        query
            pointer to the query

    @retval EOK query processed successfully
    @retval EINVAL invalid arguments

==============================================================================*/
static int ProcessQuery( FCGIProcState *pState, char *query )
{
    int result = EINVAL;
    char *pQuery = NULL;
    int i;
    int n;

    QueryFunc fn[] =
    {
        { "start=", &ProcessStartRequest },
        { "stop=", &ProcessStopRequest },
        { "restart=", &ProcessRestartRequest },
        { "list", &ProcessListRequest }
    };

    /* count the number of query processing functions */
    n = sizeof( fn ) / sizeof( QueryFunc );

    if ( ( pState != NULL ) &&
         ( query != NULL ) )
    {
        /* process the request */
        result = ProcessQueryFunctions( pState, query, fn, n );
        if ( result != EOK )
        {
	        ErrorResponse( 400, "Bad request" );
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessQueryFunctions                                                     */
/*!
    Process Variable Query functions

    The ProcessQueryFunctions function applies an array of functions to the
    variable query string, applying the functions from the function list
    as appropriate.

    @param[in]
        pState
            pointer to the FCGIProc state object

    @param[in]
        query
            pointer to the query

    @param[in]
        pFns
            array of query processing functions to possibly apply

    @param[in]
        numFuncs
            number of functions in the pFns array

    @retval EOK query processed successfully
    @retval EINVAL invalid arguments

==============================================================================*/
static int ProcessQueryFunctions( FCGIProcState *pState,
                                  char *query,
                                  QueryFunc *pFns,
                                  int numFuncs )
{
    int result = EINVAL;
    char *mutquery;
    int i;
    char *pQuery;
    char *save = NULL;
    int rc;

    if ( ( pState != NULL ) && ( query != NULL ) && ( pFns != NULL ))
    {
        /* assume everything is ok, until it is not */
        result = EOK;

        /* create a copy of the query string we can freely mutate */
        mutquery = strdup( query );
        if ( mutquery != NULL )
        {
            /* split the query on "&" */
            pQuery = strtok_r( mutquery, "&", &save );
            while ( pQuery != NULL )
            {
                /* invoked the query function */
                rc = InvokeQueryFunction( pState, pQuery, pFns, numFuncs );
                if ( rc != EOK )
                {
                    result = rc;
                }

                /* get the next token */
                pQuery = strtok_r( NULL, "&", &save );
            }

            /* free the mutable query string */
            free( mutquery );
        }
        else
        {
            result = ENOMEM;
        }
    }

    return result;
}

/*============================================================================*/
/*  InvokeQueryFunction                                                       */
/*!
    Invoke a Variable Query function

    The InvokeQueryFunction function scans the list of supplied functions
    and compares that against the supplied query argument, and invokes
    the function which matches the supplied query argument.

    @param[in]
        pState
            pointer to the FCGIProc state object

    @param[in]
        query
            pointer to the query argument

    @param[in]
        pFns
            array of query processing functions to possibly apply

    @param[in]
        numFuncs
            number of functions in the pFns array

    @retval EOK query processed successfully
    @retval EINVAL invalid arguments

==============================================================================*/
static int InvokeQueryFunction( FCGIProcState *pState,
                                char *query,
                                QueryFunc *pFns,
                                int numFuncs )
{
    int result = EINVAL;
    int i;
    char *tag;
    int (*pTagFn)( FCGIProcState *, char *) = NULL;
    bool found;
    size_t offset;

    if ( ( pState != NULL ) && ( query != NULL ) && ( pFns != NULL ) )
    {
        /* assume everything is ok until it is not */
        result = EOK;

        /* iterate through the query handlers */
        for ( i=0; i < numFuncs ; i++ )
        {
            /* get a pointer to the tag to search for */
            tag = pFns[i].tag;

            /* check if our current token starts with this tag */
            if( strstr( query, tag ) == query )
            {
                /* get a pointer to the query function */
                pTagFn = pFns[i].pTagFn;
                if( pTagFn != NULL )
                {
                    /* get the start of the query data */
                    offset = strlen( tag );

                    /* invoke the query handler */
                    result = pTagFn( pState, &query[offset] );
                    break;
                }
            }
        }
    }

    return result;

}

/*============================================================================*/
/*  ProcessStartRequest                                                       */
/*!
    Handle a process start request

    The ProcessStartRequest function starts the process specified in the
    query argument

    @param[in]
        pState
            pointer to the FCGIProc state object

    @param[in]
        query
            pointer to the name of the process to start

    @retval EOK query processed successfully
    @retval EINVAL invalid arguments

==============================================================================*/
static int ProcessStartRequest( FCGIProcState *pState, char *query )
{
    int result = EINVAL;
    char cmd[BUFSIZ];

    if ( ( pState != NULL ) &&
         ( query != NULL ) )
    {
        result = ValidateProcName( query );
        if ( result == EOK )
        {
            snprintf(cmd, BUFSIZ, "/usr/local/bin/procmon -s %s", query );
            result = ExecuteCommand(cmd, false);
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessStopRequest                                                        */
/*!
    Handle a process stop request

    The ProcessStopRequest function stops the process specified in the
    query argument

    @param[in]
        pState
            pointer to the FCGIProc state object

    @param[in]
        query
            pointer to the name of the process to start

    @retval EOK query processed successfully
    @retval EINVAL invalid arguments

==============================================================================*/
static int ProcessStopRequest( FCGIProcState *pState, char *query )
{
    int result = EINVAL;
    char cmd[BUFSIZ];

    if ( ( pState != NULL ) &&
         ( query != NULL ) )
    {
        result = ValidateProcName( query );
        if ( result == EOK )
        {
            snprintf(cmd, BUFSIZ, "/usr/local/bin/procmon -k %s", query );
            result = ExecuteCommand(cmd, false);
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessRestartRequest                                                     */
/*!
    Handle a process restart request

    The ProcessRestartRequest function stops and restarts the process
    specified in the query argument

    @param[in]
        pState
            pointer to the FCGIProc state object

    @param[in]
        query
            pointer to the name of the process to restart

    @retval EOK query processed successfully
    @retval EINVAL invalid arguments

==============================================================================*/
static int ProcessRestartRequest( FCGIProcState *pState, char *query )
{
    int result = EINVAL;
    char cmd[BUFSIZ];

    if ( ( pState != NULL ) &&
         ( query != NULL ) )
    {
        result = ValidateProcName( query );
        if ( result == EOK )
        {
            snprintf(cmd, BUFSIZ, "/usr/local/bin/procmon -r %s", query );
            result = ExecuteCommand(cmd, false);
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessListRequest                                                        */
/*!
    Handle a process stop request

    The ProcessListRequest function lists all the processes managed by
    the process manager.

    @param[in]
        pState
            pointer to the FCGIProc state object

    @param[in]
        query
            pointer to the query argument (unused)

    @retval EOK query processed successfully
    @retval EINVAL invalid arguments

==============================================================================*/
static int ProcessListRequest( FCGIProcState *pState, char *query )
{
    int result;
    char *cmd = "/usr/local/bin/procmon -o json";

    result = ExecuteCommand(cmd, true);

    return result;
}

/*============================================================================*/
/*  AllocatePOSTBuffer                                                        */
/*!
    Allocate memory for the POST buffer

    The AllocatePOSTBuffer function allocates storage space on the heap
    for a buffer to contain the POST data.  It gets the requested POST
    buffer size from the FCGIProcState object.

    @param[in]
        pState
            pointer to the FCGIProc state object containing the requested
            POST buffer size

    @retval EOK memory was successfully allocated for the POST buffer
    @retval ENOMEM could not allocate memory for the POST buffer
    @retval EINVAL invalid arguments

==============================================================================*/
static int AllocatePOSTBuffer( FCGIProcState *pState )
{
    int result = EINVAL;

    if ( pState != NULL )
    {
        if( pState->maxPostLength > 0 )
        {
            /* allocate memory for the POST buffer including a NUL terminator */
            pState->postBuffer = calloc( 1, pState->maxPostLength + 1 );
            if( pState->postBuffer != NULL )
            {
                result = EOK;
            }
            else
            {
                /* cannot allocate memory for the POST buffer */
                result = ENOMEM;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  ClearPOSTBuffer                                                           */
/*!
    Zero the memory used for the POST data

    The ClearPOSTBuffer function zeros the memory used by the POST buffer
    between requests.

    @param[in]
        pState
            pointer to the FCGIProc state object containing the POST buffer.

    @retval EOK memory was successfully allocated for the POST buffer
    @retval ENOMEM the POST buffer memory was not allocated
    @retval EINVAL invalid arguments

==============================================================================*/
static int ClearPOSTBuffer( FCGIProcState *pState )
{
    int result = EINVAL;

    if ( pState != NULL )
    {
        if ( pState->postBuffer != NULL )
        {
            /* clear the post buffer (including NUL terminator) */
            memset( pState->postBuffer, 0, pState->maxPostLength + 1 );

            result = EOK;
        }
        else
        {
            result = ENOMEM;
        }
    }

    return result;
}

/*============================================================================*/
/*  SetupTerminationHandler                                                   */
/*!
    Set up an abnormal termination handler

    The SetupTerminationHandler function registers a termination handler
    function with the kernel in case of an abnormal termination of this
    process.

==============================================================================*/
static void SetupTerminationHandler( void )
{
    static struct sigaction sigact;

    memset( &sigact, 0, sizeof(sigact) );

    sigact.sa_sigaction = TerminationHandler;
    sigact.sa_flags = SA_SIGINFO;

    sigaction( SIGTERM, &sigact, NULL );

}

/*============================================================================*/
/*  TerminationHandler                                                        */
/*!
    Abnormal termination handler

    The TerminationHandler function will be invoked in case of an abnormal
    termination of this process.

@param[in]
    signum
        The signal which caused the abnormal termination (unused)

@param[in]
    info
        pointer to a siginfo_t object (unused)

@param[in]
    ptr
        signal context information (ucontext_t) (unused)

==============================================================================*/
static void TerminationHandler( int signum, siginfo_t *info, void *ptr )
{
}

/*============================================================================*/
/*  ValidateProcName                                                          */
/*!
    Validate the process name

    The ValidateProcName function checks the specified process name
    to make sure it only contains alphanumeric characters.

    @param[in]
       procname
            pointer to the NUL terminated process name

    @retval EOK - the process name is valid
    @retval EINVAL - the process name is invalid

==============================================================================*/
static int ValidateProcName( char *procname )
{
    int result = EINVAL;
    int len;
    int i;

    if ( procname != NULL )
    {
        result = EOK;

        len = strlen( procname );
        for(i=0;i<len;i++)
        {
            if ( ! ( isalpha(procname[i]) || isdigit(procname[i]) ) )
            {
                result = EINVAL;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  ExecuteCommand                                                            */
/*!
    Execute a command and pipe the output to the output stream

    The ExecuteCommand function executes the specified command
    and redirects the command output to the FCGI output stream

    @param[in]
       cmd
            pointer to the NUL terminated command string to execute

    @param[in]
        json
            boolean indicating if JSON output is expected (true)
            or not (false)

    @retval EOK - command executed successfully
    @retval ENOENT - the command was not found
    @retval EINVAL - invalid arguments

==============================================================================*/
static int ExecuteCommand( char *cmd, bool json )
{
    int n;
    int result = EINVAL;
    char buf[BUFSIZ];
    FILE *fp_in;

    if( cmd != NULL )
    {
        /* assume command not executed until popen succeeds */
        result = ENOENT;

        /* execute the command */
        fp_in = popen( cmd, "r" );
        if( fp_in != NULL )
        {
            /* send the header */
            json ? SendJSONHeader() : SendHeader();

            do
            {
                /* read a buffer of output */
                n = fread( buf, 1, BUFSIZ, fp_in);
                if( n > 0 )
                {
                    FCGI_fwrite( buf, n, 1, FCGI_stdout );
                }
            } while( n > 0 );

            /* close the command output data stream */
            pclose( fp_in );

            /* indicate success */
            result = EOK;
        }
    }

    return result;
}

/*============================================================================*/
/*  SendHeader                                                                */
/*!
    Send a response header

    The SendHeader function sends a response header

    @retval EOK response sent successfully
    @retval EINVAL invalid arguments

==============================================================================*/
static void SendHeader( void )
{
    /* output the response header */
    printf("Status: 200 OK\r\n");
    printf("Content-Type: text/plain; charset=utf-8\r\n\r\n");
}

/*============================================================================*/
/*  SendJSONHeader                                                            */
/*!
    Send a JSON response header

    The SendJSONHeader function sends a JSON response header

    @retval EOK response sent successfully
    @retval EINVAL invalid arguments

==============================================================================*/
static void SendJSONHeader( void )
{
    /* output the response header */
    printf("Status: 200 OK\r\n");
    printf("Content-Type: application/json; charset=utf-8\r\n\r\n");
}

/*============================================================================*/
/*  ErrorResponse                                                             */
/*!
    Send an error response

    The ErrorResponse function sends an error response to the client
    using the Status header, and the status code and error description
    in a JSON object.

    @param[in]
        status
            status response code

    @param[in]
        description
            status response description

    @retval EOK the response was sent
    @retval EINVAL invalid arguments

==============================================================================*/
static int ErrorResponse( int status,  char *description )
{
    int result = EINVAL;

    if ( description != NULL )
    {
        /* output header */
        printf("Status: %d %s\r\n", status, description);
        printf("Content-Type: application/json\r\n\r\n");

        /* output body */
        printf("{\"status\": %d, \"description\" : \"%s\"}",
                status,
                description );

        result = EOK;

    }

    return result;
}

/*! @>
 * end of fcgi_proc group */

