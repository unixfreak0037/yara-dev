/*
Copyright (c) 2007. Victor M. Alvarez [plusvic@gmail.com].

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <string.h>
#include <stdio.h>

#include "filemap.h"
#include "mem.h"
#include "eval.h"
#include "lex.h"
#include "weight.h"
#include "proc.h"
#include "exe.h"
#include "regex.h"
#include "yara.h"
#include "scan.h"

#ifdef WIN32
#define snprintf _snprintf
#endif

// global thread variables
pthread_mutex_t match_lock;
int thread_count = 1; // default to using a single cpu/core
int scan_by_line = FALSE;

void threaded_scan(void * args) 
{
    THREADED_SCAN_ARGS * tscan_args = (THREADED_SCAN_ARGS *)args;
    int i;
    int error;

    MEMORY_BLOCK * block = tscan_args->block;
    YARA_CONTEXT * context = tscan_args->context;

    for (i = tscan_args->thread_index; i < block->size - 1; i += thread_count)
    {		    
        /* search for normal strings */	
        error = find_matches(   block->data[i], 
                                block->data[i + 1], 
                                block->data + i, 
                                block->size - i, 
                                block->base + i, 
                                STRING_FLAGS_HEXADECIMAL | STRING_FLAGS_ASCII, 
                                i, 
                                context);
    
        if (error != ERROR_SUCCESS)
            pthread_exit(NULL);
            //return error;
    
        /* search for wide strings */
        if ((block->data[i + 1] == 0) && (block->size > 3) && (i < block->size - 3) && (block->data[i + 3] == 0))
        {
            error = find_matches(   block->data[i], 
                                    block->data[i + 2], 
                                    block->data + i, 
                                    block->size - i, 
                                    block->base + i, 
                                    STRING_FLAGS_WIDE, 
                                    i, 
                                    context);
        
            if (error != ERROR_SUCCESS)
                pthread_exit(NULL);
                //return error;
        }	
    }

    pthread_exit(NULL);
}

void yr_init()
{
    yr_heap_alloc();
}

YARA_CONTEXT* yr_create_context()
{
    YARA_CONTEXT* context = (YARA_CONTEXT*) yr_malloc(sizeof(YARA_CONTEXT));
    
    context->rule_list.head = NULL;
    context->rule_list.tail = NULL;
    context->hash_table.non_hashed_strings = NULL;
    context->hash_table.populated = FALSE;
    context->errors = 0;
    context->error_report_function = NULL;
    context->last_error = ERROR_SUCCESS;
    context->last_error_line = 0;
    context->last_result = ERROR_SUCCESS;
    context->file_stack_ptr = 0;
    context->file_name_stack_ptr = 0;
    context->current_rule_strings = NULL;
    context->current_rule_flags = 0;
    context->inside_for = 0;
	context->namespaces = NULL;
	context->variables = NULL;
    context->allow_includes = TRUE;
	context->current_namespace = yr_create_namespace(context, "default");
	context->fast_match = FALSE;
    context->scanning_process_memory = FALSE;

    memset(context->rule_list.hash_table, 0, sizeof(context->rule_list.hash_table));
    memset(context->hash_table.hashed_strings_2b, 0, sizeof(context->hash_table.hashed_strings_2b));
    memset(context->hash_table.hashed_strings_1b, 0, sizeof(context->hash_table.hashed_strings_1b));

    // initialize predefined variables
    yr_define_string_variable(context, PREDEFINED_VAR_FILE_PATH, "");
    yr_define_boolean_variable(context, PREDEFINED_VAR_IS_EXECUTABLE, 0);
    
    return context;
    
}

void yr_destroy_context(YARA_CONTEXT* context)
{
    RULE* rule;
    RULE* next_rule;
    STRING* string;
    STRING* next_string;
    META* meta;
    META* next_meta;
    MATCH* match;
    MATCH* next_match;
	TAG* tag;
	TAG* next_tag;
	NAMESPACE* ns;
	NAMESPACE* next_ns;
    VARIABLE* variable;
	VARIABLE* next_variable;
    RULE_LIST_ENTRY* rule_list_entry;
    RULE_LIST_ENTRY* next_rule_list_entry;
	
    int i;
    
    rule = context->rule_list.head;
    
    while (rule != NULL)
    {        
        next_rule = rule->next;
        
        string = rule->string_list_head;
        
        while (string != NULL)
        {
            next_string = string->next;
            
			yr_free(string->identifier);
            yr_free(string->string);
            
            if (IS_HEX(string))
            {   
                yr_free(string->mask);
            }
            else if (IS_REGEXP(string))
            {
                regex_free(&(string->re));
            }
            
            match = string->matches_head;
            
            while (match != NULL)
            {
                next_match = match->next;
                yr_free(match->data);
                yr_free(match);
                match = next_match;
            }
            
            yr_free(string);
            string = next_string;
        }

		tag = rule->tag_list_head;

		while (tag != NULL)
		{
			next_tag = tag->next;
			
			yr_free(tag->identifier);
			yr_free(tag);
			
			tag = next_tag;
		}
		
		meta = rule->meta_list_head;

		while (meta != NULL)
		{
			next_meta = meta->next;
			
			if (meta->type == META_TYPE_STRING)
			{
                yr_free(meta->string);
			}
			
			yr_free(meta->identifier);
			yr_free(meta);
			
			meta = next_meta;
		}
        
        free_term(rule->condition);
        yr_free(rule->identifier);    
        yr_free(rule);
        rule = next_rule;
    }
	
	ns = context->namespaces;

	while(ns != NULL)
	{
		next_ns = ns->next;
		
		yr_free(ns->name);
		yr_free(ns);
		
		ns = next_ns;
	}
	
	variable = context->variables;

	while(variable != NULL)
	{
		next_variable = variable->next;
/*		
		if (->type == VARIABLE_TYPE_STRING)
		{
		    yr_free(->string);
		}
	*/	
		yr_free(variable->identifier);
		yr_free(variable);
		
		variable = next_variable;
	}
	
	while (context->file_name_stack_ptr > 0)
    {
        yr_pop_file_name(context);
    }
    
    for(i = 0; i < RULE_LIST_HASH_TABLE_SIZE; i++)
    {
        rule_list_entry = context->rule_list.hash_table[i].next;
        
        while(rule_list_entry != NULL)
        {
            next_rule_list_entry = rule_list_entry->next;
            yr_free(rule_list_entry);
            
            rule_list_entry = next_rule_list_entry;
        }
    }
    
    clear_hash_table(&context->hash_table);
	yr_free(context);
}


NAMESPACE* yr_create_namespace(YARA_CONTEXT* context, const char* name)
{
	NAMESPACE* ns = yr_malloc(sizeof(NAMESPACE));
	
	if (ns != NULL)
	{
		ns->name = yr_strdup(name);
		ns->global_rules_satisfied = FALSE;
		ns->next = context->namespaces;
		context->namespaces = ns;
	}
	
	return ns;
}


int yr_define_integer_variable(YARA_CONTEXT* context, const char* identifier, size_t value)
{
    VARIABLE* variable;

    variable = lookup_variable(context->variables, identifier);
    
    if (variable == NULL) /* variable doesn't exists, create it */
    {
        variable = (VARIABLE*) yr_malloc(sizeof(VARIABLE));
        
        if (variable != NULL)
        {
            variable->identifier = yr_strdup(identifier);      
            variable->next = context->variables;
            context->variables = variable;
        }
        else
        {
            return ERROR_INSUFICIENT_MEMORY;
        }
    }

    variable->type = VARIABLE_TYPE_INTEGER;
    variable->integer = value;
    
    return ERROR_SUCCESS;
}


int yr_define_boolean_variable(YARA_CONTEXT* context, const char* identifier, int value)
{
    VARIABLE* variable;

    variable = lookup_variable(context->variables, identifier);
    
    if (variable == NULL) /* variable doesn't exists, create it */
    {
        variable = (VARIABLE*) yr_malloc(sizeof(VARIABLE));
        
        if (variable != NULL)
        {      
            variable->identifier = yr_strdup(identifier);      
            variable->next = context->variables;
            context->variables = variable;
        }
        else
        {
            return ERROR_INSUFICIENT_MEMORY;
        }
    }

	variable->type = VARIABLE_TYPE_BOOLEAN;
    variable->boolean = value;
    
    return ERROR_SUCCESS;
}


int yr_define_string_variable(YARA_CONTEXT* context, const char* identifier, const char* value)
{
    VARIABLE* variable;

    variable = lookup_variable(context->variables, identifier);
    
    if (variable == NULL) /* variable doesn't exists, create it */
    {
        variable = (VARIABLE*) yr_malloc(sizeof(VARIABLE));
        
        if (variable != NULL)
        {
            variable->identifier = yr_strdup(identifier);    
            variable->next = context->variables;
            context->variables = variable;
        }
        else
        {
            return ERROR_INSUFICIENT_MEMORY;
        }
    }

	variable->type = VARIABLE_TYPE_STRING;
    variable->string = (char*) value;
    
    return ERROR_SUCCESS;
}

int yr_undefine_variable(YARA_CONTEXT* context, const char* identifier)
{
    VARIABLE* variable = context->variables;
    VARIABLE* previous;
    
    int found = FALSE;
    
    if (strcmp(variable->identifier, identifier) == 0)
    {
        context->variables = variable->next;
        yr_free(variable->identifier);
        yr_free(variable);
        
        found = TRUE;
    }
    else
    { 
        previous = variable;
        variable = variable->next;
    
        while (!found && variable != NULL)
        {
            if (strcmp(variable->identifier, identifier) == 0)
            {
                previous->next = variable->next;
                yr_free(variable->identifier);
                yr_free(variable);

                found = TRUE;
            }
            else 
            {
                previous = variable;
                variable = variable->next;
            }
        }
    }
    
    return (found)? ERROR_SUCCESS : ERROR_UNDEFINED_IDENTIFIER;
}



char* yr_get_current_file_name(YARA_CONTEXT* context)
{   
    if (context->file_name_stack_ptr > 0)
    {
        return context->file_name_stack[context->file_name_stack_ptr - 1];
    }
    else
    {
        return NULL;
    }
}

int yr_push_file_name(YARA_CONTEXT* context, const char* file_name)
{  
    int i;
    
    for (i = 0; i < context->file_name_stack_ptr; i++)
    {
        if (strcmp(file_name, context->file_name_stack[i]) == 0)
        {
            context->last_result = ERROR_INCLUDES_CIRCULAR_REFERENCE;
            return ERROR_INCLUDES_CIRCULAR_REFERENCE;
        }
    }

    if (context->file_name_stack_ptr < MAX_INCLUDE_DEPTH)
    { 
        context->file_name_stack[context->file_name_stack_ptr] = yr_strdup(file_name);
        context->file_name_stack_ptr++;
        return ERROR_SUCCESS;
    }
    else
    {
        context->last_result = ERROR_INCLUDE_DEPTH_EXCEEDED;
        return ERROR_INCLUDE_DEPTH_EXCEEDED;
    }
}


void yr_pop_file_name(YARA_CONTEXT* context)
{  
    if (context->file_name_stack_ptr > 0)
    {
        context->file_name_stack_ptr--;
        yr_free(context->file_name_stack[context->file_name_stack_ptr]);
        context->file_name_stack[context->file_name_stack_ptr] = NULL;  
    }
}


int yr_push_file(YARA_CONTEXT* context, FILE* fh)
{  
    int i;

    if (context->file_stack_ptr < MAX_INCLUDE_DEPTH)
    { 
        context->file_stack[context->file_stack_ptr] = fh;
        context->file_stack_ptr++;
        return ERROR_SUCCESS;
    }
    else
    {
        context->last_result = ERROR_INCLUDE_DEPTH_EXCEEDED;
        return ERROR_INCLUDE_DEPTH_EXCEEDED;
    }
}


FILE* yr_pop_file(YARA_CONTEXT* context)
{  
    FILE* result = NULL;
    
    if (context->file_stack_ptr > 0)
    {
        context->file_stack_ptr--;
        result = context->file_stack[context->file_stack_ptr];
    }
    
    return result;
}

int yr_compile_file(FILE* rules_file, YARA_CONTEXT* context)
{	
    return parse_rules_file(rules_file, context);
}

int yr_compile_string(const char* rules_string, YARA_CONTEXT* context)
{	
    return parse_rules_string(rules_string, context);
}

int yr_scan_mem_blocks(MEMORY_BLOCK* block, YARA_CONTEXT* context, YARACALLBACK callback, void* user_data)
{
    int error;
    int global_rules_satisfied;
	unsigned int i;	
	int is_executable;
    int is_file;
    int all_preconditions_failed = TRUE;
	
	RULE* rule;
	NAMESPACE* ns;
	EVALUATION_CONTEXT eval_context;

    // thread variables
    pthread_t * threads = NULL;
	
	if (block->size < 2)
        return ERROR_SUCCESS;

	if (!context->hash_table.populated)
	{
        populate_hash_table(&context->hash_table, &context->rule_list);
	}
	
	eval_context.file_size = block->size;
    eval_context.mem_block = block;
    eval_context.entry_point = 0;
	
    is_executable = is_pe(block->data, block->size) || is_elf(block->data, block->size) || context->scanning_process_memory;
    is_file = !context->scanning_process_memory;

    yr_define_boolean_variable(context, PREDEFINED_VAR_IS_EXECUTABLE, is_executable);

	clear_marks(&context->rule_list);

    // evaluate precondition for each rule
	rule = context->rule_list.head;
    while (rule != NULL)
    {
        if (rule->precondition != NULL)
            if (evaluate(rule->precondition, &eval_context) == 0) 
                rule->flags |= RULE_FLAGS_FAILED_PRECONDITION;
            else
                all_preconditions_failed = FALSE;
        else
            all_preconditions_failed = FALSE;

        rule = rule->next;
    }

    // if all the preconditions failed then we're done
    if (all_preconditions_failed)
    {
        //printf("all preconditions failed\n");
        return ERROR_SUCCESS;
    }
	
	while (block != NULL)
	{
	    if (eval_context.entry_point == 0)
	    {
	        if (context->scanning_process_memory)
	        {
	            eval_context.entry_point = get_entry_point_address(block->data, block->size, block->base);
	        }
	        else
	        {
	            eval_context.entry_point = get_entry_point_offset(block->data, block->size);
            }
        }

        pthread_mutex_init(&match_lock, NULL);
        
        // array to store thread handles
        threads = (pthread_t *)malloc(sizeof(pthread_t) * thread_count);

        for (i = 0; i < thread_count && i < block->size - 1; i++) 
        {
            THREADED_SCAN_ARGS * args = (THREADED_SCAN_ARGS *)malloc(sizeof(THREADED_SCAN_ARGS));
            args->thread_index = i;
            args->block = block;
            args->context = context;

            //attr = pth_attr_new();
            //pth_attr_set(attr, PTH_ATTR_NAME, "threaded_scanner");
            //pth_attr_set(attr, PTH_ATTR_STACK_SIZE, 64 * 1024);
            //pth_attr_set(attr, PTH_ATTR_JOINABLE, FALSE);
            pthread_create(&threads[i], NULL, threaded_scan, args);
        }

        // wait for all the threads to finish
        for (i = 0; i < thread_count; i++)
        {
            pthread_join(threads[i], NULL);
        }
    	
        block = block->next;
    }
	
	rule = context->rule_list.head;
	
	/* initialize global rules flag for all namespaces */
	
	ns = context->namespaces;
	
	while(ns != NULL)
	{
		ns->global_rules_satisfied = TRUE;
		ns = ns->next;
	}
	
	/* evaluate global rules */
	
	while (rule != NULL)
	{	
		if (rule->flags & RULE_FLAGS_GLOBAL)
		{
            if (! (rule->flags & RULE_FLAGS_FAILED_PRECONDITION))
            {
                eval_context.rule = rule;
                
                if (evaluate(rule->condition, &eval_context))
                {
                    rule->flags |= RULE_FLAGS_MATCH;
                }
                else
                {
                    rule->ns->global_rules_satisfied = FALSE;
                }
                
                if (!(rule->flags & RULE_FLAGS_PRIVATE))
                {
                    if (callback(rule, user_data) != 0)
                    {
                        return ERROR_CALLBACK_ERROR;
                    }
                }
            }
		}
			
		rule = rule->next;
	}
	
	/* evaluate the rest of the rules rules */

	rule = context->rule_list.head;
	
	while (rule != NULL)
	{
		/* 
		   skip global rules, privates rules, rules that don't need to be
		   evaluated due to some global rule unsatisfied in it's namespace,
           and rules that have failed the precondition
		*/
		
		if (rule->flags & RULE_FLAGS_GLOBAL || rule->flags & RULE_FLAGS_PRIVATE || !rule->ns->global_rules_satisfied
            || rule->flags & RULE_FLAGS_FAILED_PRECONDITION)  
		{
			rule = rule->next;
			continue;
		}

	  
		if ((is_executable  || !(rule->flags & RULE_FLAGS_REQUIRE_EXECUTABLE)) &&
		    (is_file        || !(rule->flags & RULE_FLAGS_REQUIRE_FILE)))
		{
		    eval_context.rule = rule;
		    
		    if (evaluate(rule->condition, &eval_context))
    		{
                rule->flags |= RULE_FLAGS_MATCH;
    		}
		}
		
		switch (callback(rule, user_data))
		{
		    case CALLBACK_ABORT:
                return ERROR_SUCCESS;
                
            case CALLBACK_ERROR:
                return ERROR_CALLBACK_ERROR;
		}
		
		rule = rule->next;
	}
	
	return ERROR_SUCCESS;
}

int yr_scan_mem(unsigned char* buffer, size_t buffer_size, YARA_CONTEXT* context, YARACALLBACK callback, void* user_data)
{
    MEMORY_BLOCK block;
    
    block.data = buffer;
    block.size = buffer_size;
    block.base = 0;
    block.next = NULL;
    
    return yr_scan_mem_blocks(&block, context, callback, user_data);
}

int yr_scan_file(const char* file_path, YARA_CONTEXT* context, YARACALLBACK callback, void* user_data)
{
	MAPPED_FILE mfile;
	int result;
    char * start = NULL;
    char * stop = NULL;

    result = map_file(file_path, &mfile);
	
	if (result == ERROR_SUCCESS)
	{
        yr_define_string_variable(context, PREDEFINED_VAR_FILE_PATH, file_path);

        // default is to scan the entire file
        if (! scan_by_line)
        {
            result = yr_scan_mem(mfile.data, mfile.size, context, callback, user_data);
            unmap_file(&mfile);
            return result;
        }

        // otherwise we break it up by line
        start = stop = mfile.data;
        while (start < (mfile.data + mfile.size)) 
        {
            if (*stop == '\n' || *stop == '\r')
            {
                stop++;

                // if this is \r\n then move ahead one more
                if (stop < (mfile.data + mfile.size) && *(stop - 1) == '\r' && *stop == '\n')
                    stop++;

                // scan this much stuff
                if ((stop - start) > 0)
                {
                    result = yr_scan_mem(start, stop - start, context, callback, user_data);
                    if (result != ERROR_SUCCESS)
                        break;

                    start = stop;
                }
            }

            stop++;
        }

		unmap_file(&mfile);
	} 
    else
    {
        fprintf(stderr, "unable to scan file %s error code %d\n", file_path, result);
    }
		
	return result;
}

int yr_scan_proc(int pid, YARA_CONTEXT* context, YARACALLBACK callback, void* user_data)
{
    
    MEMORY_BLOCK* first_block;
    MEMORY_BLOCK* next_block;
    MEMORY_BLOCK* block;
        
    int result = get_process_memory(pid, &first_block);

    if (result == ERROR_SUCCESS)
    {
        context->scanning_process_memory = TRUE;
        result = yr_scan_mem_blocks(first_block, context, callback, user_data);
    }
    
    if (result == ERROR_SUCCESS)
    {  
        block = first_block;
    
        while (block != NULL)
        {
            next_block = block->next;
        
            yr_free(block->data);
            yr_free(block);   
        
            block = next_block;   
        }
    }

    return result;
}


char* yr_get_error_message(YARA_CONTEXT* context, char* buffer, int buffer_size)
{
    switch(context->last_error)
	{
		case ERROR_INSUFICIENT_MEMORY:
		    snprintf(buffer, buffer_size, "not enough memory");
			break;
		case ERROR_DUPLICATE_RULE_IDENTIFIER:
			snprintf(buffer, buffer_size, "duplicate rule identifier \"%s\"", context->last_error_extra_info);
			break;
		case ERROR_DUPLICATE_STRING_IDENTIFIER:
			snprintf(buffer, buffer_size, "duplicate string identifier \"%s\"", context->last_error_extra_info);
			break;
		case ERROR_DUPLICATE_TAG_IDENTIFIER:
			snprintf(buffer, buffer_size, "duplicate tag identifier \"%s\"", context->last_error_extra_info);			
			break;		
		case ERROR_DUPLICATE_META_IDENTIFIER:
			snprintf(buffer, buffer_size, "duplicate metadata identifier \"%s\"", context->last_error_extra_info);			
			break;	
		case ERROR_INVALID_CHAR_IN_HEX_STRING:
		   	snprintf(buffer, buffer_size, "invalid char in hex string \"%s\"", context->last_error_extra_info);
			break;
		case ERROR_MISMATCHED_BRACKET:
			snprintf(buffer, buffer_size, "mismatched bracket in string \"%s\"", context->last_error_extra_info);
			break;
		case ERROR_SKIP_AT_END:		
			snprintf(buffer, buffer_size, "skip at the end of string \"%s\"", context->last_error_extra_info);	
		    break;
		case ERROR_INVALID_SKIP_VALUE:
			snprintf(buffer, buffer_size, "invalid skip in string \"%s\"", context->last_error_extra_info);
			break;
		case ERROR_UNPAIRED_NIBBLE:
			snprintf(buffer, buffer_size, "unpaired nibble in string \"%s\"", context->last_error_extra_info);
			break;
		case ERROR_CONSECUTIVE_SKIPS:
			snprintf(buffer, buffer_size, "two consecutive skips in string \"%s\"", context->last_error_extra_info);			
			break;
		case ERROR_MISPLACED_WILDCARD_OR_SKIP:
			snprintf(buffer, buffer_size, "misplaced wildcard or skip at string \"%s\", wildcards and skips are only allowed after the first byte of the string", context->last_error_extra_info);
		    break;
		case ERROR_MISPLACED_OR_OPERATOR:
		    snprintf(buffer, buffer_size, "misplaced OR (|) operator at string \"%s\"", context->last_error_extra_info);
			break;
		case ERROR_NESTED_OR_OPERATION:
	        snprintf(buffer, buffer_size, "nested OR (|) operator at string \"%s\"", context->last_error_extra_info);
		    break;		
		case ERROR_INVALID_OR_OPERATION_SYNTAX:
		    snprintf(buffer, buffer_size, "invalid syntax at hex string \"%s\"", context->last_error_extra_info);
			break;
		case ERROR_SKIP_INSIDE_OR_OPERATION:
    		snprintf(buffer, buffer_size, "skip inside an OR (|) operation at string \"%s\"", context->last_error_extra_info);
    		break;
		case ERROR_UNDEFINED_STRING:
            snprintf(buffer, buffer_size, "undefined string \"%s\"", context->last_error_extra_info);
			break;
		case ERROR_UNDEFINED_IDENTIFIER:
		    snprintf(buffer, buffer_size, "undefined identifier \"%s\"", context->last_error_extra_info);
			break;
		case ERROR_UNREFERENCED_STRING:
		    snprintf(buffer, buffer_size, "unreferenced string \"%s\"", context->last_error_extra_info);
			break;
	    case ERROR_INCORRECT_VARIABLE_TYPE:
		    snprintf(buffer, buffer_size, "external variable \"%s\" has an incorrect type for this operation", context->last_error_extra_info);
			break;
		case ERROR_MISPLACED_ANONYMOUS_STRING:
	        snprintf(buffer, buffer_size, "wrong use of anonymous string");
		    break;		
		case ERROR_INVALID_REGULAR_EXPRESSION:
		case ERROR_SYNTAX_ERROR:
		    snprintf(buffer, buffer_size, "%s", context->last_error_extra_info);
			break;
		case ERROR_INCLUDES_CIRCULAR_REFERENCE:
		    snprintf(buffer, buffer_size, "include circular reference");
                case ERROR_INCLUDE_DEPTH_EXCEEDED:
                    snprintf(buffer, buffer_size, "too many levels of included rules");
            break;		    
	}
	
    return buffer;
}


int yr_calculate_rules_weight(YARA_CONTEXT* context)
{
    STRING_LIST_ENTRY* entry;

    int i,j, count, weight = 0;

    if (!context->hash_table.populated)
    {        
        populate_hash_table(&context->hash_table, &context->rule_list);
    }
    
    for (i = 0; i < 256; i++)
    {   
        for (j = 0; j < 256; j++)
        {
            entry = context->hash_table.hashed_strings_2b[i][j];
        
            count = 0;
        
            while (entry != NULL)
            {         
                weight += string_weight(entry->string, 1);               
                entry = entry->next;
                count++;
            }
            
            weight += count;
        }
        
        entry = context->hash_table.hashed_strings_1b[i];
    
        count = 0;
    
        while (entry != NULL)
        {         
            weight += string_weight(entry->string, 2);               
            entry = entry->next;
            count++;
        }
    }
    
    entry = context->hash_table.non_hashed_strings;
    
    while (entry != NULL)
    {
        weight += string_weight(entry->string, 4);
    }
    
    return weight;
}

