
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_RECORDS 2500
#define MAX_PATTERNS 7500
#define MAX_FIELDS 16
#define FIELD_SIZE 4096
#define NAME_SIZE 512

unsigned char coverage[MAX_PATTERNS][MAX_RECORDS];
char pattern_names[MAX_PATTERNS][NAME_SIZE];

int pattern_count = 0;
int record_count = 0;

typedef struct {
    int iterations;
    int gain_checks;
    int covered;
    double runtime;
} Result;

/* Read CSV rows, including quoted fields */
int read_csv_row(FILE *fp, char fields[MAX_FIELDS][FIELD_SIZE]) {
    int field = 0, pos = 0, quotes = 0, c;
    int got_any = 0;

    for (int i = 0; i < MAX_FIELDS; i++)
        fields[i][0] = '\0';

    while ((c = fgetc(fp)) != EOF) {
        got_any = 1;

        if (c == '"') {
            if (quotes) {
                int next = fgetc(fp);

                if (next == '"') {
                    if (pos < FIELD_SIZE - 1)
                        fields[field][pos++] = '"';
                } else {
                    quotes = 0;
                    if (next != EOF)
                        ungetc(next, fp);
                }
            } else {
                quotes = 1;
            }
        }
        else if (c == ',' && !quotes) {
            fields[field][pos] = '\0';
            field++;
            pos = 0;

            if (field >= MAX_FIELDS)
                return field;
        }
        else if ((c == '\n' || c == '\r') && !quotes) {
            fields[field][pos] = '\0';

            if (c == '\r') {
                int next = fgetc(fp);
                if (next != '\n' && next != EOF)
                    ungetc(next, fp);
            }

            return field + 1;
        }
        else {
            if (pos < FIELD_SIZE - 1)
                fields[field][pos++] = (char)c;
        }
    }

    if (!got_any)
        return 0;

    fields[field][pos] = '\0';
    return field + 1;
}

/* Find or create a candidate pattern */
int find_or_add_pattern(const char *name) {
    for (int i = 0; i < pattern_count; i++) {
        if (strcmp(pattern_names[i], name) == 0)
            return i;
    }

    if (pattern_count >= MAX_PATTERNS)
        return -1;

    strncpy(pattern_names[pattern_count], name, NAME_SIZE - 1);
    pattern_names[pattern_count][NAME_SIZE - 1] = '\0';

    return pattern_count++;
}

/* Add record-pattern relationship */
void add_membership(const char *type, const char *value, int record) {
    char key[NAME_SIZE];

    snprintf(key, sizeof(key), "%s: %.470s", type, value);

    int p = find_or_add_pattern(key);

    if (p >= 0)
        coverage[p][record] = 1;
}

/* Reset data before each experiment */
void reset_data(void) {
    memset(coverage, 0, sizeof(coverage));
    pattern_count = 0;
    record_count = 0;
}

/*
   Mode 1: EventTemplate
   Mode 2: EventTemplate + Component
   Mode 3: EventTemplate + Component + Level
*/
int load_dataset(const char *filename, int limit, int mode) {
    FILE *fp = fopen(filename, "r");

    if (!fp) {
        perror("CSV file");
        return 0;
    }

    char fields[MAX_FIELDS][FIELD_SIZE];

    int nfields = read_csv_row(fp, fields);

    if (nfields <= 0) {
        fclose(fp);
        return 0;
    }

    while (record_count < limit &&
           record_count < MAX_RECORDS &&
           (nfields = read_csv_row(fp, fields)) > 0) {

        if (nfields < 9 || fields[8][0] == '\0')
            continue;

        /* EventTemplate: column 8 */
        add_membership("TEMPLATE", fields[8], record_count);

        /* Component: column 5 */
        if (mode >= 2 && fields[5][0] != '\0')
            add_membership("COMPONENT", fields[5], record_count);

        /* Level: column 4 */
        if (mode >= 3 && fields[4][0] != '\0')
            add_membership("LEVEL", fields[4], record_count);

        record_count++;
    }

    fclose(fp);
    return record_count;
}

/* Greedy Maximum Coverage */
Result run_greedy(int K) {
    int selected[MAX_PATTERNS] = {0};
    int seen[MAX_RECORDS] = {0};

    Result result = {0, 0, 0, 0.0};

    clock_t start = clock();

    for (int k = 0; k < K; k++) {
        int best = -1;
        int best_gain = 0;

        for (int p = 0; p < pattern_count; p++) {
            if (selected[p])
                continue;

            int gain = 0;

            for (int r = 0; r < record_count; r++) {
                result.gain_checks++;

                if (coverage[p][r] && !seen[r])
                    gain++;
            }

            if (gain > best_gain) {
                best_gain = gain;
                best = p;
            }
        }

        if (best == -1 || best_gain == 0)
            break;

        selected[best] = 1;
        result.iterations++;

        for (int r = 0; r < record_count; r++) {
            if (coverage[best][r])
                seen[r] = 1;
        }
    }

    clock_t end = clock();

    result.runtime =
        (double)(end - start) / CLOCKS_PER_SEC;

    for (int r = 0; r < record_count; r++) {
        if (seen[r])
            result.covered++;
    }

    return result;
}

/* Display experiment results */
void run_scenario(
    const char *label,
    const char *filename,
    int limit,
    int K,
    int mode
) {
    reset_data();

    if (!load_dataset(filename, limit, mode)) {
        printf("\nCould not load dataset for %s\n", label);
        return;
    }

    Result result = run_greedy(K);

    printf("\n====================================\n");
    printf("%s\n", label);
    printf("====================================\n");

    printf("Dataset records      : %d\n", record_count);
    printf("Candidate patterns   : %d\n", pattern_count);
    printf("Selection budget K   : %d\n", K);
    printf("Iterations           : %d\n", result.iterations);
    printf("Gain checks          : %d\n", result.gain_checks);

    printf("Records covered      : %d/%d\n",
           result.covered, record_count);

    printf("Coverage             : %.2f%%\n",
           100.0 * result.covered / record_count);

    printf("Runtime              : %.6f seconds\n",
           result.runtime);
}

/* Main function */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s HDFS_2k.log_structured.csv [records] [K]\n",
               argv[0]);
        return 1;
    }

    int limit = (argc >= 3) ? atoi(argv[2]) : 2000;
    int K = (argc >= 4) ? atoi(argv[3]) : 10;

    if (limit < 1 || limit > MAX_RECORDS ||
        K < 1 || K > MAX_PATTERNS) {
        printf("Invalid record limit or K.\n");
        return 1;
    }

    printf("\nLOGINTEL - HDFS DATASET ANALYSIS\n");
    printf("Dataset: %s\n", argv[1]);

    printf("\nComparing three dataset-derived configurations.\n");

    run_scenario(
        "BEST-CASE CONFIGURATION: EventTemplate",
        argv[1], limit, K, 1
    );

    run_scenario(
        "AVERAGE-CASE CONFIGURATION: EventTemplate + Component",
        argv[1], limit, K, 2
    );

    run_scenario(
        "WORST-CASE CONFIGURATION: EventTemplate + Component + Level",
        argv[1], limit, K, 3
    );

    printf("\nAll experiments completed.\n");

    return 0;
}
