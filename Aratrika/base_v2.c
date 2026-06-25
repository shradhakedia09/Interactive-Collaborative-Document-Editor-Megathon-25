#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <libwebsockets.h> // libwebsockets library

// --- Configuration and Global State ---

#define MAX_BUFFER_SIZE 1024
#define EDITOR_PORT 7681
#define CODE_LEN 12

static int g_force_exit = 0;

// --- User-Provided Data Structures ---

typedef struct
{
    char code[CODE_LEN];
    char perm[16]; // "VIEW" or "EDIT"
    char filepath[256];
    char assigned_by[64]; // Owner
    int active;
    char hierarchy;     // 'A'-'Z' for hierarchical, '0' for general
    int temp_priority; // Tie-breaker
} EditUser;

typedef struct
{
    char **lines;
    char *line_hierarchy;
    int *line_temp_priority;
    size_t line_count;
    int is_hierarchical;
} DocumentState;

typedef struct
{
    char *line_edit; // new content
    EditUser user;
    int has_edit;
} PendingEdit;

// Global state for in-memory management (simplified for a single instance)
EditUser *edit_users = NULL;
int edit_user_count = 0;
int edit_user_capacity = 0;
time_t last_priority_update = 0;
PendingEdit *pending_edits = NULL;
size_t doc_lines_count = 0;

// --- WebSocket Context Data (Per-Connection) ---
struct per_session_data__editor {
    char user_id[64]; // Firebase UID from client
    EditUser collab_user; // Resolved user permissions/hierarchy
};


// --- Utility and File Functions (Copied from User Snippet) ---

void ensure_docs_dir()
{
    struct stat st = {0};
    if (stat("docs", &st) == -1)
        mkdir("docs", 0700);
}

// NOTE: generate_code, prompt_line are excluded as they are UI/CLI functions.

void add_edit_user(EditUser user)
{
    // This is simplified. In a real LWS server, we'd use a hash map or similar structure
    // to map the client's session to the EditUser object. For now, we only store the
    // resolved data in the per-session struct.
    // This function is kept for consistency with the user's original logic structure.
    if (edit_user_count >= edit_user_capacity)
    {
        edit_user_capacity = edit_user_capacity == 0 ? 10 : edit_user_capacity * 2;
        edit_users = realloc(edit_users, edit_user_capacity * sizeof(EditUser));
    }
    edit_users[edit_user_count++] = user;
}

int save_code_mapping(const char *code, const char *perm, const char *filepath, const char *owner, char hierarchy)
{
    FILE *fp = fopen("codes.csv", "a");
    if (!fp)
        return 0;
    fprintf(fp, "%s,%s,%s,%s,%c,1\n", code, perm, filepath, owner, hierarchy ? hierarchy : '0');
    fclose(fp);
    return 1;
}

/**
 * @brief Looks up code details from codes.csv.
 */
int lookup_code(const char *code, EditUser *user_out)
{
    FILE *fp = fopen("codes.csv", "r");
    if (!fp)
        return 0;
    char line[512];
    while (fgets(line, sizeof(line), fp))
    {
        char c[CODE_LEN], perm[16], path[256], owner[64], h_char;
        int active;
        // Use sscanf to safely parse the CSV line
        if (sscanf(line, "%[^,],%[^,],%[^,],%[^,],%c,%d", 
                   c, perm, path, owner, &h_char, &active) == 6)
        {
            if (strcmp(c, code) == 0 && active == 1)
            {
                // Fill the output structure
                strncpy(user_out->code, c, CODE_LEN);
                strncpy(user_out->perm, perm, 16);
                strncpy(user_out->filepath, path, 256);
                strncpy(user_out->assigned_by, owner, 64);
                user_out->hierarchy = h_char;
                user_out->active = active;
                user_out->temp_priority = rand() % 1000; // Assign temp priority on lookup
                
                fclose(fp);
                return 1;
            }
        }
    }
    fclose(fp);
    return 0;
}

// NOTE: revoke_code, create_document, load_document, save_document, free_document, list_my_documents are excluded
// or kept simplified, as they involve heavy I/O and command line interaction not suitable for a non-blocking server callback.
// They will be called once an edit is resolved.

void randomize_priorities()
{
    // Re-randomizes priorities for all active connections (not fully implemented here,
    // but the logic is needed for resolving conflicts based on temp_priority).
    if (time(NULL) - last_priority_update > 3600) {
        // Simple placeholder for priority update logic
        lwsl_notice("Re-randomizing priorities...\n");
        last_priority_update = time(NULL);
    }
}

void init_pending_edits(DocumentState *doc)
{
    // This function would normally ensure a new document state is loaded,
    // but in a real server, this would be per-document, not global.
    // We keep it for structure.
    if (pending_edits) {
        for (size_t i = 0; i < doc_lines_count; i++) {
            if (pending_edits[i].line_edit) free(pending_edits[i].line_edit);
        }
        free(pending_edits);
    }
    doc_lines_count = doc->line_count;
    pending_edits = malloc(doc_lines_count * sizeof(PendingEdit));
    if (!pending_edits) {
        lwsl_err("Failed to allocate pending_edits buffer.\n");
        return;
    }
    for (size_t i = 0; i < doc_lines_count; i++)
    {
        pending_edits[i].line_edit = NULL;
        pending_edits[i].has_edit = 0;
    }
}

/**
 * @brief Core conflict resolution logic (copied from user snippet).
 * @param doc The current document state (in-memory).
 * @param line_num The line being edited.
 * @param user The user submitting the edit.
 * @param new_text The new content for the line.
 */
void submit_edit_buffer(DocumentState *doc, int line_num, EditUser *user, const char *new_text)
{
    if (line_num < 0 || line_num >= doc->line_count)
        return;

    // Simulate randomizing priorities if necessary (for tie-breaking)
    randomize_priorities(); 

    if (pending_edits[line_num].has_edit)
    {
        PendingEdit *prev = &pending_edits[line_num];
        
        // Check Hierarchical priority (A is higher than B)
        int is_higher_hierarchy = user->hierarchy != '0' && user->hierarchy < prev->user.hierarchy;
        // Check Tie-breaker (higher priority value wins)
        int is_higher_priority = user->temp_priority > prev->user.temp_priority;
        
        // Decision Logic
        if (doc->is_hierarchical)
        {
            if (is_higher_hierarchy || (user->hierarchy == prev->user.hierarchy && is_higher_priority))
            {
                free(prev->line_edit);
                prev->line_edit = strdup(new_text);
                prev->user = *user;
                lwsl_notice("Line %d: Edit buffered (Higher priority user won).\n", line_num);
            } else {
                 lwsl_notice("Line %d: Edit rejected (Lower priority).\n", line_num);
            }
        }
        else // General mode (only uses temp_priority)
        {
            if (is_higher_priority)
            {
                free(prev->line_edit);
                prev->line_edit = strdup(new_text);
                prev->user = *user;
                lwsl_notice("Line %d: Edit buffered (Higher priority user won).\n", line_num);
            } else {
                 lwsl_notice("Line %d: Edit rejected (Lower priority).\n", line_num);
            }
        }
    }
    else // No pending edit, accept immediately
    {
        pending_edits[line_num].line_edit = strdup(new_text);
        pending_edits[line_num].user = *user;
        pending_edits[line_num].has_edit = 1;
        lwsl_notice("Line %d: Edit buffered (First edit for this cycle).\n", line_num);
    }
}

void resolve_pending_edits(DocumentState *doc)
{
    // This is the CRITICAL point where changes are written to the main document state
    for (size_t i = 0; i < doc->line_count; i++)
    {
        if (pending_edits[i].has_edit)
        {
            free(doc->lines[i]);
            doc->lines[i] = strdup(pending_edits[i].line_edit);
            doc->line_hierarchy[i] = pending_edits[i].user.hierarchy;
            doc->line_temp_priority[i] = pending_edits[i].user.temp_priority;
            
            lwsl_notice("Line %zu resolved and applied by user %s (Hier: %c).\n", 
                        i, pending_edits[i].user.assigned_by, pending_edits[i].user.hierarchy);

            // Cleanup pending edit
            free(pending_edits[i].line_edit);
            pending_edits[i].line_edit = NULL;
            pending_edits[i].has_edit = 0;
            
            // In a real application, we would call lws_callback_on_writable_all_protocol
            // here to broadcast the change to all connected clients.
        }
    }
    // After resolving, save the document to disk (simplified placeholder)
    // save_document(doc, doc->filepath); 
}


// --- LWS Protocol Callback ---

/**
 * @brief The core callback function for our WebSocket protocol.
 */
static int callback_editor(struct lws *wsi, enum lws_callback_reasons reason,
                           void *user, void *in, size_t len) {
    
    struct per_session_data__editor *pss = (struct per_session_data__editor *)user;
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            lwsl_notice("Connection established. Session created.\n");
            break;

        case LWS_CALLBACK_RECEIVE:
        {
            char buffer[MAX_BUFFER_SIZE + 1];
            size_t copy_len = len < MAX_BUFFER_SIZE ? len : MAX_BUFFER_SIZE;
            memcpy(buffer, in, copy_len);
            buffer[copy_len] = '\0';
            lwsl_debug("Received data: %s\n", buffer);

            // Simple check to determine the type of JSON message received
            if (strstr(buffer, "\"type\":\"ACCESS_CODE_CHECK\"")) 
            {
                // --- 1. ACCESS CODE CHECK HANDLER ---
                char code[CODE_LEN] = {0};
                char user_id[64] = {0};
                // Mock JSON parsing using sscanf to extract necessary values
                // NOTE: This is fragile and relies on specific string formatting.
                if (sscanf(buffer, "{\"type\":\"ACCESS_CODE_CHECK\",\"code\":\"%11[^\"]\",\"user_id\":\"%63[^\"]\"}", 
                           code, user_id) == 2)
                {
                    EditUser found_user = {0};
                    if (lookup_code(code, &found_user) && strcmp(found_user.perm, "EDIT") == 0)
                    {
                        // Authentication Success
                        pss->collab_user = found_user;
                        strncpy(pss->user_id, user_id, 64);
                        
                        // Send success message back to client
                        char response[256];
                        snprintf(response, sizeof(response), 
                                 "{\"type\":\"AUTH_SUCCESS\",\"hierarchy\":\"%c\",\"message\":\"Authenticated\"}", 
                                 found_user.hierarchy);

                        lws_write(wsi, (unsigned char *)response + LWS_PRE, strlen(response), LWS_WRITE_TEXT);
                        lwsl_notice("Client authenticated for code %s (Hier: %c, UID: %s)\n", 
                                    code, found_user.hierarchy, user_id);
                        
                        // TODO: Load the document state associated with found_user.filepath
                        // and call init_pending_edits() here.

                    } else {
                        // Authentication Failure
                        const char *response = "{\"type\":\"AUTH_FAILURE\",\"message\":\"Invalid or VIEW-only code.\"}";
                        lws_write(wsi, (unsigned char *)response + LWS_PRE, strlen(response), LWS_WRITE_TEXT);
                        lwsl_warn("Authentication failed for code %s.\n", code);
                    }
                } else {
                    lwsl_warn("ACCESS_CODE_CHECK payload malformed.\n");
                }
            } 
            else if (strstr(buffer, "\"type\":\"SUBMIT_EDIT\"")) 
            {
                // --- 2. SUBMIT EDIT HANDLER ---
                if (pss->collab_user.active == 0) {
                     lwsl_warn("Unauthenticated client attempted edit submission.\n");
                     return 0; // Ignore unauthenticated edits
                }
                
                // Example of simple parsing for SUBMIT_EDIT (actual logic is much harder with full JSON)
                // For simplicity, we assume the edit is valid and extract line/content.
                // In a real scenario, we'd need a robust JSON parser (like jansson).
                int line_num;
                // Since full JSON parsing is complex in vanilla C, this step is symbolic.
                // It assumes the line_num and new_text can be extracted.
                // For this example, we assume line_num=0 and new_text is the whole payload (excluding type).
                line_num = 0; 
                char *new_text_start = strstr(buffer, "\"new_text\":\"");
                if (new_text_start) {
                    new_text_start += 12; // Move past the key and quotes
                    char *new_text_end = strchr(new_text_start, '"');
                    if (new_text_end) *new_text_end = '\0';
                }
                
                DocumentState mock_doc = { .line_count = 10, .is_hierarchical = 1 }; // Mock document state
                // Note: The real document must be loaded from file based on pss->collab_user.filepath
                // and pss->collab_user.hierarchy.

                // Mock initialization if not already done (in a real app, this is per-document)
                if (!pending_edits) {
                    DocumentState temp_doc = { .line_count = 10, .is_hierarchical = 1 };
                    init_pending_edits(&temp_doc);
                }

                submit_edit_buffer(&mock_doc, line_num, &pss->collab_user, new_text_start ? new_text_start : "Empty Edit");
                
                // Immediately resolve edits (simplifying the server loop logic)
                resolve_pending_edits(&mock_doc);

            } else {
                lwsl_warn("Received unknown message type.\n");
            }
        }
        break;

        case LWS_CALLBACK_CLOSED:
            lwsl_notice("Connection closed.\n");
            // TODO: Remove pss->collab_user from any active user lists
            break;

        default:
            break;
    }
    return 0;
}

// --- Protocol Definition and Server Setup (Remains the same) ---

static struct lws_protocols protocols[] = {
    {
        "editor-protocol", 
        callback_editor, 
        sizeof(struct per_session_data__editor), // Allocate memory for session data
        MAX_BUFFER_SIZE, 
    },
    { NULL, NULL, 0, 0 } /* End of list */
};

static void sigint_handler(int sig) {
    if (sig == SIGINT) {
        lwsl_notice("SIGINT received, shutting down gracefully...\n");
        g_force_exit = 1;
    }
}

int main(void) {
    struct lws_context *context;
    struct lws_context_creation_info info;

    signal(SIGINT, sigint_handler);
    memset(&info, 0, sizeof(info));
    info.port = EDITOR_PORT;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.fd_limit_per_thread = 10; 

    lwsl_notice("Starting Rich Text Editor Server on port %d...\n", EDITOR_PORT);
    lwsl_notice("Waiting for WebSocket connections...\n");
    lwsl_notice("NOTE: Document creation is still command-line driven. Run the menu to generate codes.\n");
    
    // Set LWS logging level (optional)
    lws_set_log_level(LLL_NOTICE | LLL_WARN | LLL_ERR, NULL);
    
    // Ensure document directory exists
    ensure_docs_dir();

    context = lws_create_context(&info);
    if (!context) {
        lwsl_err("lws_create_context failed. Is port %d already in use?\n", EDITOR_PORT);
        return 1;
    }

    while (!g_force_exit) {
        if (lws_service(context, 50) < 0) {
            break;
        }
    }

    lwsl_notice("Server shutting down...\n");
    lws_context_destroy(context);
    free(edit_users); 

    return 0;
}
