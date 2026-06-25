#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define CODE_LEN 6

typedef struct
{
    char code[CODE_LEN];
    char perm[16]; // "VIEW" or "EDIT"
    char filepath[256];
    char assigned_by[64]; // Owner
    int active;
    char hierarchy;    // 'A'-'Z' for hierarchical, 0 for general
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

EditUser *edit_users = NULL;
int edit_user_count = 0;
int edit_user_capacity = 0;
time_t last_priority_update = 0;

PendingEdit *pending_edits = NULL;
size_t doc_lines_count = 0;

// Utility functions
void ensure_docs_dir()
{
    struct stat st = {0};
    if (stat("docs", &st) == -1)
        mkdir("docs", 0700);
}

void generate_code(char *out, size_t len)
{
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for (size_t i = 0; i < len - 1; i++)
        out[i] = charset[rand() % strlen(charset)];
    out[len - 1] = '\0';
}

void prompt_line(const char *msg, char *buf, size_t sz)
{
    printf("%s", msg);
    fflush(stdout);
    if (fgets(buf, sz, stdin))
        buf[strcspn(buf, "\n")] = 0; // remove newline
}

void add_edit_user(EditUser user)
{
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

int lookup_code(const char *code, char *perm_out, size_t perm_sz, char *fp_out, size_t fp_sz, char *owner_out, char *hierarchy_out)
{
    FILE *fp = fopen("codes.csv", "r");
    if (!fp)
        return 0;
    char line[512];
    while (fgets(line, sizeof(line), fp))
    {
        char c[CODE_LEN], perm[16], path[256], owner[64], h;
        int active;
        if (sscanf(line, "%[^,],%[^,],%[^,],%[^,],%c,%d", c, perm, path, owner, &h, &active) == 6)
        {
            if (strcmp(c, code) == 0 && active == 1)
            {
                strncpy(perm_out, perm, perm_sz);
                strncpy(fp_out, path, fp_sz);
                strncpy(owner_out, owner, 64);
                *hierarchy_out = h;
                fclose(fp);
                return 1;
            }
        }
    }
    fclose(fp);
    return 0;
}

void revoke_code(const char *target_code)
{
    FILE *fp = fopen("codes.csv", "r");
    if (!fp)
    {
        printf("No codes found\n");
        return;
    }
    FILE *tmp = fopen("codes_tmp.csv", "w");
    if (!tmp)
    {
        fclose(fp);
        printf("Error\n");
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), fp))
    {
        char c[CODE_LEN], perm[16], path[256], owner[64], h;
        int active;
        if (sscanf(line, "%[^,],%[^,],%[^,],%[^,],%c,%d", c, perm, path, owner, &h, &active) == 6)
        {
            if (strcmp(c, target_code) == 0)
                fprintf(tmp, "%s,%s,%s,%s,%c,0\n", c, perm, path, owner, h);
            else
                fputs(line, tmp);
        }
    }
    fclose(fp);
    fclose(tmp);
    remove("codes.csv");
    rename("codes_tmp.csv", "codes.csv");
    printf("Code %s revoked\n", target_code);
}

void create_document(const char *filepath, int is_hierarchical)
{
    FILE *fp = fopen(filepath, "w");
    if (!fp)
    {
        printf("Error creating file\n");
        return;
    }
    fprintf(fp, "{\\rtf1\\ansi\\deff0\n");
    printf("Enter document content line by line (type <<END>> to finish):\n");
    char *line = NULL;
    size_t len = 0;
    while (1)
    {
        printf("> ");
        fflush(stdout);
        ssize_t read = getline(&line, &len, stdin);
        if (read == -1)
            break;
        line[strcspn(line, "\n")] = 0;
        if (strcmp(line, "<<END>>") == 0)
            break;
        fprintf(fp, "%s \\par\n", line);
    }
    free(line);
    fprintf(fp, "}");
    fclose(fp);
    printf("Document saved: %s\n", filepath);
}

DocumentState load_document(const char *filepath, int is_hierarchical)
{
    DocumentState doc = {0};
    doc.lines = NULL;
    doc.line_hierarchy = NULL;
    doc.line_temp_priority = NULL;
    doc.line_count = 0;
    doc.is_hierarchical = is_hierarchical;
    FILE *fp = fopen(filepath, "r");
    if (!fp)
    {
        printf("Error opening %s\n", filepath);
        return doc;
    }
    char *line = NULL;
    size_t len = 0;
    while (getline(&line, &len, fp) != -1)
    {
        line[strcspn(line, "\n")] = 0;
        doc.lines = realloc(doc.lines, (doc.line_count + 1) * sizeof(char *));
        doc.line_hierarchy = realloc(doc.line_hierarchy, (doc.line_count + 1) * sizeof(char));
        doc.line_temp_priority = realloc(doc.line_temp_priority, (doc.line_count + 1) * sizeof(int));
        doc.lines[doc.line_count] = strdup(line);
        doc.line_hierarchy[doc.line_count] = 0;
        doc.line_temp_priority[doc.line_count] = 0;
        doc.line_count++;
    }
    free(line);
    fclose(fp);
    return doc;
}

void randomize_priorities()
{
    for (int i = 0; i < edit_user_count; i++)
        edit_users[i].temp_priority = rand() % 1000;
    last_priority_update = time(NULL);
}

void init_pending_edits(DocumentState *doc)
{
    doc_lines_count = doc->line_count;
    pending_edits = malloc(doc_lines_count * sizeof(PendingEdit));
    for (size_t i = 0; i < doc_lines_count; i++)
    {
        pending_edits[i].line_edit = NULL;
        pending_edits[i].has_edit = 0;
    }
}

void submit_edit_buffer(DocumentState *doc, int line_num, EditUser *user, const char *new_text)
{
    if (line_num < 0 || line_num >= doc->line_count)
        return;
    if (pending_edits[line_num].has_edit)
    {
        PendingEdit *prev = &pending_edits[line_num];
        if (doc->is_hierarchical)
        {
            if (user->hierarchy < prev->user.hierarchy ||
                (user->hierarchy == prev->user.hierarchy && user->temp_priority > prev->user.temp_priority))
            {
                free(prev->line_edit);
                prev->line_edit = strdup(new_text);
                prev->user = *user;
            }
        }
        else
        {
            if (user->temp_priority > prev->user.temp_priority)
            {
                free(prev->line_edit);
                prev->line_edit = strdup(new_text);
                prev->user = *user;
            }
        }
    }
    else
    {
        pending_edits[line_num].line_edit = strdup(new_text);
        pending_edits[line_num].user = *user;
        pending_edits[line_num].has_edit = 1;
    }
}

void resolve_pending_edits(DocumentState *doc)
{
    for (size_t i = 0; i < doc->line_count; i++)
    {
        if (pending_edits[i].has_edit)
        {
            free(doc->lines[i]);
            doc->lines[i] = strdup(pending_edits[i].line_edit);
            doc->line_hierarchy[i] = pending_edits[i].user.hierarchy;
            doc->line_temp_priority[i] = pending_edits[i].user.temp_priority;
            free(pending_edits[i].line_edit);
            pending_edits[i].line_edit = NULL;
            pending_edits[i].has_edit = 0;
        }
    }
}

void save_document(DocumentState *doc, const char *filepath)
{
    FILE *fp = fopen(filepath, "w");
    if (!fp)
    {
        printf("Error saving %s\n", filepath);
        return;
    }
    fprintf(fp, "{\\rtf1\\ansi\\deff0\n");
    for (size_t i = 0; i < doc->line_count; i++)
        fprintf(fp, "%s \\par\n", doc->lines[i]);
    fprintf(fp, "}");
    fclose(fp);
}

void free_document(DocumentState *doc)
{
    for (size_t i = 0; i < doc->line_count; i++)
        free(doc->lines[i]);
    free(doc->lines);
    free(doc->line_hierarchy);
    free(doc->line_temp_priority);
    if (pending_edits)
    {
        free(pending_edits);
        pending_edits = NULL;
    }
}

void list_my_documents(const char *owner)
{
    FILE *fp = fopen("codes.csv", "r");
    if (!fp)
    {
        printf("No documents found\n");
        return;
    }
    char line[512];
    int index = 1;
    char files[1024][256];
    while (fgets(line, sizeof(line), fp))
    {
        char c[CODE_LEN], perm[16], path[256], own[64], h;
        int active;
        if (sscanf(line, "%[^,],%[^,],%[^,],%[^,],%c,%d", c, perm, path, own, &h, &active) == 6)
        {
            if (strcmp(owner, own) == 0 && active == 1)
            {
                int duplicate = 0;
                for (int j = 0; j < index - 1; j++)
                    if (strcmp(files[j], path) == 0)
                        duplicate = 1;
                if (!duplicate)
                {
                    strcpy(files[index - 1], path);
                    printf("%d) %s\n", index, path);
                    index++;
                }
            }
        }
    }
    fclose(fp);
    if (index == 1)
    {
        printf("No documents found.\n");
        return;
    }
    printf("Select document number to view (0 to cancel): ");
    int choice;
    scanf("%d", &choice);
    getchar();
    if (choice >= 1 && choice < index)
    {
        DocumentState doc = load_document(files[choice - 1], 1);
        for (size_t i = 0; i < doc.line_count; i++)
            printf("%s\n", doc.lines[i]);
        free_document(&doc);
    }
}

int main()
{
    srand(time(NULL));
    ensure_docs_dir();
    char user_name[64];
    prompt_line("Enter your name: ", user_name, sizeof(user_name));
    while (1)
    {
        printf("\n===== MENU =====\n1) Create new document\n2) View my documents\n3) Enter access code\n4) Revoke code (owner only)\n5) Quit\n> ");
        int choice;
        scanf("%d", &choice);
        getchar();
        if (choice == 1)
        {
            char docname[128];
            prompt_line("Document name: ", docname, sizeof(docname));
            char filepath[256];
            snprintf(filepath, sizeof(filepath), "docs/%s.rtf", docname);

            int fmt = 0;
            while (1)
            {
                printf("Format: 1) General 2) Hierarchical > ");
                if (scanf("%d", &fmt) != 1)
                {
                    while (getchar() != '\n')
                        ;
                    printf("Invalid input. Enter 1 or 2.\n");
                    continue;
                }
                getchar();
                if (fmt == 1 || fmt == 2)
                    break;
                printf("Invalid choice. Enter 1 or 2.\n");
            }

            create_document(filepath, fmt == 2 ? 1 : 0);

            char view_code[CODE_LEN], edit_code[CODE_LEN];
            generate_code(view_code, CODE_LEN);
            do
            {
                generate_code(edit_code, CODE_LEN);
            } while (strcmp(view_code, edit_code) == 0);
            save_code_mapping(view_code, "VIEW", filepath, user_name, 0);
            printf("VIEW code: %s\n", view_code);

            char hierarchy_level = 0;
            if (fmt == 2)
            {
                while (1)
                {
                    char temp[8];
                    prompt_line("Assign hierarchy letter (A-Z) for EDIT code: ", temp, sizeof(temp));
                    if (strlen(temp) == 1 && temp[0] >= 'A' && temp[0] <= 'Z')
                    {
                        hierarchy_level = temp[0];
                        break;
                    }
                    printf("Invalid input. Enter a single letter A-Z.\n");
                }
            }
            save_code_mapping(edit_code, "EDIT", filepath, user_name, hierarchy_level);
            printf("EDIT code: %s (Hierarchy: %c)\n", edit_code, hierarchy_level ? hierarchy_level : '0');
        }
        else if (choice == 2)
        {
            list_my_documents(user_name);
        }
        else if (choice == 3)
        {
            char code[CODE_LEN], perm[16], filepath[256], owner[64], hier;
            prompt_line("Enter access code: ", code, sizeof(code));
            if (lookup_code(code, perm, sizeof(perm), filepath, sizeof(filepath), owner, &hier))
            {
                if (strcmp(perm, "VIEW") == 0)
                {
                    DocumentState doc = load_document(filepath, hier != 0);
                    for (size_t i = 0; i < doc.line_count; i++)
                        printf("%s\n", doc.lines[i]);
                    free_document(&doc);
                }
                else
                    printf("Use external system to submit edits. Hierarchy: %c\n", hier ? hier : '0');
            }
            else
                printf("Invalid/revoked code.\n");
        }
        else if (choice == 4)
        {
            char code[CODE_LEN];
            while (1)
            {
                printf("Enter code to revoke (or type 'quit' to cancel): ");
                if (fgets(code, sizeof(code), stdin))
                {
                    code[strcspn(code, "\n")] = 0; // remove newline
                }
                if (strcmp(code, "quit") == 0)
                    break; // cancel
                char perm[16], filepath[256], owner_tmp[64], hier;
                if (lookup_code(code, perm, sizeof(perm), filepath, sizeof(filepath), owner_tmp, &hier))
                {
                    revoke_code(code);
                    break; // valid code revoked
                }
                else
                {
                    printf("Invalid code. Please enter a valid active code or type 'quit' to cancel.\n");
                }
            }
        }

        else if (choice == 5)
            break;
        else
            printf("Invalid choice.\n");
    }
    free(edit_users);
    return 0;
}
