#include "postgres_fe.h"

#include <signal.h>
#include <sys/stat.h>
#ifndef WIN32
#include <unistd.h>
#else
#include <io.h>
#endif

#include "common.h"
#include "common/logging.h"
#include "copy.h"
#include "libpq-fe.h"
#include "pqexpbuffer.h"
#include "prompt.h"
#include "settings.h"
#include "stringutils.h"
#include <pthread.h>
pthread_mutex_t copy_conn_mutex = PTHREAD_MUTEX_INITIALIZER;

struct copy_thread_data {
    char *buffer;
    size_t buffer_size;
    int thread_id;
    PGconn *conn;
    bool success;
};
#define NUM_COPY_THREADS 4

struct copy_options
{
	char	   *before_tofrom;
	char	   *after_tofrom;
	char	   *file;
	bool		program;
	bool		psql_inout;
	bool		from;
	bool		preview;
	int         thread_count;
};

static bool
is_large_file(const char *filename)
{
	struct stat st;
	
	if (stat(filename, &st) != 0)
		return false;
	
	return st.st_size > (10 * 1024 * 1024);
}

static int
prompt_for_thread_count(int default_threads)
{
	char response[32];
	int thread_count;
	
	printf("\nLarge file detected. How many threads would you like to use? [%d]: ", default_threads);
	fflush(stdout);
	
	if (fgets(response, sizeof(response), stdin) == NULL)
		return default_threads;
	
	/* Remove trailing newline */
	if (response[strlen(response) - 1] == '\n')
		response[strlen(response) - 1] = '\0';
	
	/* If empty response, use default */
	if (response[0] == '\0')
		return default_threads;
	
	thread_count = atoi(response);
	
	/* Validate thread count (minimum 1, maximum 32) */
	if (thread_count < 1)
	{
		pg_log_error("Thread count must be at least 1, using 1 thread");
		return 1;
	}
	else if (thread_count > 32)
	{
		pg_log_error("Thread count cannot exceed 32, using 32 threads");
		return 32;
	}
	
	return thread_count;
}

static void
free_copy_options(struct copy_options *ptr)
{
	if (!ptr)
		return;
	free(ptr->before_tofrom);
	free(ptr->after_tofrom);
	free(ptr->file);
	free(ptr);
}

static void
xstrcat(char **var, const char *more)
{
	char	   *newvar;

	newvar = psprintf("%s%s", *var, more);
	free(*var);
	*var = newvar;
}

static bool
preview_copy_data(struct copy_options *opts)
{
	FILE *preview_stream = NULL;
	char line[1024];
	int rows_to_preview = 10;
	int row_count = 0;
	bool success = true;

	printf("\nCOPY Operation Preview:\n");
	printf("----------------------\n");
	
	if (opts->from) {
		printf("Direction: FROM\n");
		if (opts->file) {
			printf("Source: %s%s\n", 
			       opts->program ? "PROGRAM '" : "File '",
			       opts->file);
		} else if (opts->psql_inout) {
			printf("Source: stdin\n");
		}
	} else {
		printf("Direction: TO\n");
		if (opts->file) {
			printf("Destination: %s%s\n", 
			       opts->program ? "PROGRAM '" : "File '",
			       opts->file);
		} else if (opts->psql_inout) {
			printf("Destination: stdout\n");
		}
	}
	
	printf("Query/Table: %s\n", opts->before_tofrom);
	if (opts->after_tofrom)
		printf("Options: %s\n", opts->after_tofrom);
	
	printf("\nPreview data (%d rows):\n", rows_to_preview);
	printf("----------------------\n");

	if (opts->from && opts->file) {
		if (opts->program) {
			fflush(NULL);
			errno = 0;
			preview_stream = popen(opts->file, PG_BINARY_R);
		} else {
			preview_stream = fopen(opts->file, PG_BINARY_R);
		}
		
		if (!preview_stream) {
			if (opts->program)
				pg_log_error("could not execute command \"%s\" for preview: %m", opts->file);
			else
				pg_log_error("%s: %m", opts->file);
			return false;
		}
		
		while (row_count < rows_to_preview && fgets(line, sizeof(line), preview_stream)) {
			printf("%s", line);
			row_count++;
			
			if (line[strlen(line)-1] != '\n')
				printf("\n");
		}
		
		if (row_count == 0) {
			printf("(No data or empty file)\n");
		}
		
		if (opts->program) {
			int pclose_rc = pclose(preview_stream);
			if (pclose_rc != 0) {
				if (pclose_rc < 0)
					pg_log_error("could not close pipe to external command: %m");
				else {
					char *reason = wait_result_to_str(pclose_rc);
					pg_log_error("%s: %s", opts->file, reason ? reason : "");
					free(reason);
				}
				success = false;
			}
		} else {
			if (fclose(preview_stream) != 0) {
				pg_log_error("%s: %m", opts->file);
				success = false;
			}
		}
	} else if (opts->from && opts->psql_inout) {
		printf("(Preview not available for stdin input)\n");
	} else {
		printf("(Preview not available for output operations)\n");
	}
	
	printf("----------------------\n");
	printf("Proceed with COPY? (y/n): ");
	fflush(stdout);
	
	char response;
	int c;
	
	response = getchar();
	
	while ((c = getchar()) != '\n' && c != EOF);
	
	if (response != 'y' && response != 'Y') {
		pg_log_error("Copy operation cancelled by user");
		return false;
	}
	
	return success;
}

static struct copy_options *
parse_slash_copy(const char *args)
{
	struct copy_options *result;
	char	   *token;
	const char *whitespace = " \t\n\r";
	char		nonstd_backslash = standard_strings() ? 0 : '\\';

	if (!args)
	{
		pg_log_error("\\copy: arguments required");
		return NULL;
	}

	result = pg_malloc0(sizeof(struct copy_options));
	result->before_tofrom = pg_strdup("");
	result->thread_count = 4;

	token = strtokx(args, whitespace, ".,()", "\"",
					0, false, false, pset.encoding);
	if (!token)
		goto error;
	if (pg_strcasecmp(token, "preview") == 0)
	{
		result->preview = true;
		token = strtokx(NULL, whitespace, ".,()", "\"",
						0, false, false, pset.encoding);
		if (!token)
			goto error;
	}

	if (pg_strcasecmp(token, "binary") == 0)
	{
		xstrcat(&result->before_tofrom, token);
		token = strtokx(NULL, whitespace, ".,()", "\"",
						0, false, false, pset.encoding);
		if (!token)
			goto error;
	}

	if (token[0] == '(')
	{
		int			parens = 1;

		while (parens > 0)
		{
			xstrcat(&result->before_tofrom, " ");
			xstrcat(&result->before_tofrom, token);
			token = strtokx(NULL, whitespace, "()", "\"'",
							nonstd_backslash, true, false, pset.encoding);
			if (!token)
				goto error;
			if (token[0] == '(')
				parens++;
			else if (token[0] == ')')
				parens--;
		}
	}

	xstrcat(&result->before_tofrom, " ");
	xstrcat(&result->before_tofrom, token);
	token = strtokx(NULL, whitespace, ".,()", "\"",
					0, false, false, pset.encoding);
	if (!token)
		goto error;

	if (token[0] == '.')
	{
		xstrcat(&result->before_tofrom, token);
		token = strtokx(NULL, whitespace, ".,()", "\"",
						0, false, false, pset.encoding);
		if (!token)
			goto error;
		xstrcat(&result->before_tofrom, token);
		token = strtokx(NULL, whitespace, ".,()", "\"",
						0, false, false, pset.encoding);
		if (!token)
			goto error;
	}

	if (token[0] == '(')
	{
		for (;;)
		{
			xstrcat(&result->before_tofrom, " ");
			xstrcat(&result->before_tofrom, token);
			token = strtokx(NULL, whitespace, "()", "\"",
							0, false, false, pset.encoding);
			if (!token)
				goto error;
			if (token[0] == ')')
				break;
		}
		xstrcat(&result->before_tofrom, " ");
		xstrcat(&result->before_tofrom, token);
		token = strtokx(NULL, whitespace, ".,()", "\"",
						0, false, false, pset.encoding);
		if (!token)
			goto error;
	}

	if (pg_strcasecmp(token, "from") == 0)
		result->from = true;
	else if (pg_strcasecmp(token, "to") == 0)
		result->from = false;
	else
		goto error;

	token = strtokx(NULL, whitespace, ";", "'",
					0, false, false, pset.encoding);
	if (!token)
		goto error;

	if (pg_strcasecmp(token, "program") == 0)
	{
		int			toklen;

		token = strtokx(NULL, whitespace, ";", "'",
						0, false, false, pset.encoding);
		if (!token)
			goto error;

		toklen = strlen(token);
		if (token[0] != '\'' || toklen < 2 || token[toklen - 1] != '\'')
			goto error;

		strip_quotes(token, '\'', 0, pset.encoding);

		result->program = true;
		result->file = pg_strdup(token);
	}
	else if (pg_strcasecmp(token, "stdin") == 0 ||
			 pg_strcasecmp(token, "stdout") == 0)
	{
		result->file = NULL;
	}
	else if (pg_strcasecmp(token, "pstdin") == 0 ||
			 pg_strcasecmp(token, "pstdout") == 0)
	{
		result->psql_inout = true;
		result->file = NULL;
	}
	else
	{
		strip_quotes(token, '\'', 0, pset.encoding);
		result->file = pg_strdup(token);
		expand_tilde(&result->file);
	}

	token = strtokx(NULL, "", NULL, NULL,
					0, false, false, pset.encoding);
	if (token)
		result->after_tofrom = pg_strdup(token);

	return result;

error:
	if (token)
		pg_log_error("\\copy: parse error at \"%s\"", token);
	else
		pg_log_error("\\copy: parse error at end of line");
	free_copy_options(result);

	return NULL;
}

bool
do_copy(const char *args)
{
	PQExpBufferData query;
	FILE	   *copystream;
	struct copy_options *options;
	bool		success;

	options = parse_slash_copy(args);

	if (!options)
		return false;
		if (options->from && options->file && !options->program)
		{
			if (is_large_file(options->file))
			{
				options->thread_count = prompt_for_thread_count(options->thread_count);
				
				/* Set environment variable for thread count */
				char thread_env[32];
				snprintf(thread_env, sizeof(thread_env), "PSQL_COPY_THREADS=%d", options->thread_count);
				putenv(strdup(thread_env)); /* Use strdup to ensure the string persists */
			}
		}
	if (options->preview)
	{
		bool preview_success = preview_copy_data(options);
		if (!preview_success) {
			free_copy_options(options);
			return false;
		}
		options->preview = false;
	}

	if (options->file && !options->program)
		canonicalize_path_enc(options->file, pset.encoding);

	if (options->from)
	{
		if (options->file)
		{
			if (options->program)
			{
				fflush(NULL);
				errno = 0;
				copystream = popen(options->file, PG_BINARY_R);
			}
			else
				copystream = fopen(options->file, PG_BINARY_R);
		}
		else if (!options->psql_inout)
			copystream = pset.cur_cmd_source;
		else
			copystream = stdin;
	}
	else
	{
		if (options->file)
		{
			if (options->program)
			{
				fflush(NULL);
				disable_sigpipe_trap();
				errno = 0;
				copystream = popen(options->file, PG_BINARY_W);
			}
			else
				copystream = fopen(options->file, PG_BINARY_W);
		}
		else if (!options->psql_inout)
			copystream = pset.queryFout;
		else
			copystream = stdout;
	}

	if (!copystream)
	{
		if (options->program)
			pg_log_error("could not execute command \"%s\": %m",
						 options->file);
		else
			pg_log_error("%s: %m",
						 options->file);
		free_copy_options(options);
		return false;
	}

	if (!options->program)
	{
		struct stat st;
		int			result;

		if ((result = fstat(fileno(copystream), &st)) < 0)
			pg_log_error("could not stat file \"%s\": %m",
						 options->file);

		if (result == 0 && S_ISDIR(st.st_mode))
			pg_log_error("%s: cannot copy from/to a directory",
						 options->file);

		if (result < 0 || S_ISDIR(st.st_mode))
		{
			fclose(copystream);
			free_copy_options(options);
			return false;
		}
	}

	initPQExpBuffer(&query);
	printfPQExpBuffer(&query, "COPY ");
	appendPQExpBufferStr(&query, options->before_tofrom);
	if (options->from)
		appendPQExpBufferStr(&query, " FROM STDIN ");
	else
		appendPQExpBufferStr(&query, " TO STDOUT ");
	if (options->after_tofrom)
		appendPQExpBufferStr(&query, options->after_tofrom);

	pset.copyStream = copystream;
	success = SendQuery(query.data);
	pset.copyStream = NULL;
	termPQExpBuffer(&query);

	if (options->file != NULL)
	{
		if (options->program)
		{
			int			pclose_rc = pclose(copystream);

			if (pclose_rc != 0)
			{
				if (pclose_rc < 0)
					pg_log_error("could not close pipe to external command: %m");
				else
				{
					char	   *reason = wait_result_to_str(pclose_rc);

					pg_log_error("%s: %s", options->file,
								 reason ? reason : "");
					free(reason);
				}
				success = false;
			}
			SetShellResultVariables(pclose_rc);
			restore_sigpipe_trap();
		}
		else
		{
			if (fclose(copystream) != 0)
			{
				pg_log_error("%s: %m", options->file);
				success = false;
			}
		}
	}
	free_copy_options(options);
	return success;
}

static void*
process_copy_out_chunk(void *arg)
{
    struct copy_thread_data *data = (struct copy_thread_data *)arg;
    
    if (fwrite(data->buffer, 1, data->buffer_size, (FILE*)data->conn) != data->buffer_size) {
        data->success = false;
        pg_log_error("Thread %d: failed to write COPY data", data->thread_id);
    } else {
        data->success = true;
    }
    
    return NULL;
}

bool
handleCopyOut(PGconn *conn, FILE *copystream, PGresult **res)
{
    bool        OK = true;
    char       *buf;
    int         ret;
    
   
    for (;;)
    {
        ret = PQgetCopyData(conn, &buf, 0);

        if (ret < 0)
            break;

        if (buf)
        {
            if (OK && copystream && fwrite(buf, 1, ret, copystream) != ret)
            {
                pg_log_error("could not write COPY data: %m");
                OK = false;
            }
            PQfreemem(buf);
        }
    }

    if (OK && copystream && fflush(copystream))
    {
        pg_log_error("could not write COPY data: %m");
        OK = false;
    }

    if (ret == -2)
    {
        pg_log_error("COPY data transfer failed: %s", PQerrorMessage(conn));
        OK = false;
    }

    *res = PQgetResult(conn);
    if (PQresultStatus(*res) != PGRES_COMMAND_OK)
    {
        pg_log_info("%s", PQerrorMessage(conn));
        OK = false;
    }

    return OK;
}

#define COPYBUFSIZ 8192
#define THREAD_BUFFER_SIZE (COPYBUFSIZ * 4) 
static void* 
process_copy_chunk(void *arg)
{
    struct copy_thread_data *data = (struct copy_thread_data *)arg;
    
    /* Lock the mutex before writing to the connection */
    pthread_mutex_lock(&copy_conn_mutex);
    
    /* Process the buffer */
    if (PQputCopyData(data->conn, data->buffer, data->buffer_size) <= 0) {
        data->success = false;
        pg_log_error("Thread %d: failed to put copy data", data->thread_id);
    } else {
        data->success = true;
    }
    
    /* Unlock the mutex */
    pthread_mutex_unlock(&copy_conn_mutex);
    
    return NULL;
}
bool
handleCopyIn(PGconn *conn, FILE *copystream, bool isbinary, PGresult **res)
{
    bool        OK;
    bool        showprompt;
    pthread_t   *threads;
    struct copy_thread_data *thread_data;
    char       **thread_buffers;
    bool        threads_initialized = false;
    int         i;
    int         thread_count = NUM_COPY_THREADS;  /* Use the defined default */
    char       *thread_env;
    
    /* Get thread count from environment if available */
    thread_env = getenv("PSQL_COPY_THREADS");
    if (thread_env != NULL) {
        int env_count = atoi(thread_env);
        if (env_count > 0 && env_count <= 32)
            thread_count = env_count;
    }
    
    /* Allocate thread arrays dynamically based on thread_count */
    threads = (pthread_t *) malloc(thread_count * sizeof(pthread_t));
    thread_data = (struct copy_thread_data *) malloc(thread_count * sizeof(struct copy_thread_data));
    thread_buffers = (char **) malloc(thread_count * sizeof(char *));
    
    if (threads == NULL || thread_data == NULL || thread_buffers == NULL)
    {
        pg_log_error("Out of memory allocating thread structures");
        if (threads) free(threads);
        if (thread_data) free(thread_data);
        if (thread_buffers) free(thread_buffers);
        return false;
    }
    
    for (i = 0; i < thread_count; i++) {
        thread_buffers[i] = malloc(THREAD_BUFFER_SIZE);
        if (thread_buffers[i] == NULL) {
            pg_log_error("Out of memory allocating thread buffer");
            
            while (--i >= 0)
                free(thread_buffers[i]);
                
            free(threads);
            free(thread_data);
            free(thread_buffers);
            return false;
        }
    }
    
    if (sigsetjmp(sigint_interrupt_jmp, 1) != 0)
    {
        PQputCopyEnd(conn, (PQprotocolVersion(conn) < 3) ? NULL : 
                    _("canceled by user"));
        
        if (threads_initialized) {
            for (i = 0; i < thread_count; i++) {
                pthread_join(threads[i], NULL);
                free(thread_buffers[i]);
            }
        }
        
        free(threads);
        free(thread_data);
        free(thread_buffers);
        
        OK = false;
        goto copyin_cleanup;
    }

    if (isatty(fileno(copystream)))
    {
        showprompt = true;
        if (!pset.quiet)
            puts(_("Enter data to be copied followed by a newline.\n"
                   "End with a backslash and a period on a line by itself, or an EOF signal."));
    }
    else
        showprompt = false;

    OK = true;
    threads_initialized = true;
    if (isbinary)
    {
        char buffer[COPYBUFSIZ];
        
        if (showprompt)
        {
            const char *prompt = get_prompt(PROMPT_COPY, NULL);
            fputs(prompt, stdout);
            fflush(stdout);
        }

        for (;;)
        {
            int         buflen;

            sigint_interrupt_enabled = true;
            buflen = fread(buffer, 1, COPYBUFSIZ, copystream);
            sigint_interrupt_enabled = false;

            if (buflen <= 0)
                break;

            if (PQputCopyData(conn, buffer, buflen) <= 0)
            {
                OK = false;
                break;
            }
        }
    }
    else
    {
        bool        copydone = false;
        size_t      *buflen = (size_t *) malloc(thread_count * sizeof(size_t));
        bool        at_line_begin = true;
        int         current_thread = 0;
        bool        *thread_active = (bool *) malloc(thread_count * sizeof(bool));

        if (buflen == NULL || thread_active == NULL) {
            pg_log_error("Out of memory allocating thread state");
            free(buflen);
            free(thread_active);
            for (i = 0; i < thread_count; i++)
                free(thread_buffers[i]);
            free(threads);
            free(thread_data);
            free(thread_buffers);
            return false;
        }
        
        /* Initialize thread state arrays */
        for (i = 0; i < thread_count; i++) {
            buflen[i] = 0;
            thread_active[i] = false;
            thread_data[i].thread_id = i;
            thread_data[i].conn = conn;
            thread_data[i].buffer = thread_buffers[i];
            thread_data[i].buffer_size = 0;
            thread_data[i].success = true;
        }

        while (!copydone)
        {
            char *fgresult;
            
            if (at_line_begin && showprompt)
            {
                const char *prompt = get_prompt(PROMPT_COPY, NULL);
                fputs(prompt, stdout);
                fflush(stdout);
            }

            if (thread_active[current_thread]) {
                pthread_join(threads[current_thread], NULL);
                thread_active[current_thread] = false;
                
                if (!thread_data[current_thread].success) {
                    OK = false;
                    break;
                }
            }

            sigint_interrupt_enabled = true;
            
            fgresult = fgets(thread_buffers[current_thread] + buflen[current_thread], 
                             THREAD_BUFFER_SIZE - buflen[current_thread], 
                             copystream);
            
            sigint_interrupt_enabled = false;

            if (!fgresult)
                copydone = true;
            else
            {
                int         linelen = strlen(fgresult);
                buflen[current_thread] += linelen;

                if (thread_buffers[current_thread][buflen[current_thread] - 1] == '\n')
                {
                    if (at_line_begin)
                    {
                        if ((linelen == 3 && memcmp(fgresult, "\\.\n", 3) == 0) ||
                            (linelen == 4 && memcmp(fgresult, "\\.\r\n", 4) == 0))
                        {
                            copydone = true;
                        }
                    }

                    if (copystream == pset.cur_cmd_source)
                    {
                        pset.lineno++;
                        pset.stmt_lineno++;
                    }
                    at_line_begin = true;
                }
                else
                    at_line_begin = false;
            }

            if (buflen[current_thread] >= THREAD_BUFFER_SIZE - 128 || 
                (copydone && buflen[current_thread] > 0))
            {
                thread_data[current_thread].buffer_size = buflen[current_thread];
                
                if (pthread_create(&threads[current_thread], NULL, 
                                  process_copy_chunk, &thread_data[current_thread]) != 0) {
                    if (PQputCopyData(conn, thread_buffers[current_thread], 
                                     buflen[current_thread]) <= 0) {
                        OK = false;
                        break;
                    }
                } else {
                    thread_active[current_thread] = true;
                }
                
                buflen[current_thread] = 0;
                current_thread = (current_thread + 1) % thread_count;
            }
        }

        /* Wait for all active threads to finish */
        for (i = 0; i < thread_count; i++) {
            if (thread_active[i]) {
                pthread_join(threads[i], NULL);
                if (!thread_data[i].success) {
                    OK = false;
                }
            }
        }
        
        free(buflen);
        free(thread_active);
    }
    
    /* Free allocated resources */
    for (i = 0; i < thread_count; i++) {
        free(thread_buffers[i]);
    }
    free(thread_buffers);
    free(thread_data);
    free(threads);

    if (ferror(copystream))
        OK = false;

    if (PQputCopyEnd(conn,
                     (OK || PQprotocolVersion(conn) < 3) ? NULL :
                     _("aborted because of read failure")) <= 0)
        OK = false;

copyin_cleanup:

    clearerr(copystream);

    while (*res = PQgetResult(conn), PQresultStatus(*res) == PGRES_COPY_IN)
    {
        OK = false;
        PQclear(*res);
        PQputCopyEnd(conn,
                     (PQprotocolVersion(conn) < 3) ? NULL :
                     _("trying to exit copy mode"));
    }
    if (PQresultStatus(*res) != PGRES_COMMAND_OK)
    {
        pg_log_info("%s", PQerrorMessage(conn));
        OK = false;
    }

    return OK;
}