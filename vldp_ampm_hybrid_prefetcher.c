#include <stdio.h>
#include <stdlib.h>

#include "inc/prefetcher.h"
#include "inc/utlist.h"
#include "inc/uthash.h"


// VLDP Helper functions and data structures

#define DRAM_QUEUE 100
#define PREFETCH_QUEUE 100
#define ROW_HIT_PER_PAGE 3

#define CACHE_FILL_PREFETCH_THRESHOLD 1

#define MAX_DEGREE 3
int MAX_DEGREE_PREFETCHED = 3;

#define DEGREE1_THRESHOLD 25
#define DEGREE2_THRESHOLD 50
#define DEGREE3_THRESHOLD 75

#define PRINT_CACHE_MISS_DETAILS 0
#define PRINT_ACCESSED_SERVICED_PREDICTED_TIME 0
typedef enum {
    PREFETCH, CACHE_ACCESS, DEMAND_SERVICED, PREFETCH_SERVICED
} access_type_t;

#if VLDP_DEBUG
#define VLDP_DEBUG_MSG(...) printf(__VA_ARGS__)
#else
#define VLDP_DEBUG_MSG(...)
#endif

#define PRINT_TABLE_INTERVAL 10000000

#define MIN_TAG_LENGTH 0
#define MIN_DEGREE 1
#define ACCURACY_FLAG 0
#define ACCURACY_THRESHOLD 2

//Shared Prediction Tables
#define PER_CORE_PREDICTION_TABLES 1

#define NUM_DELTAS_TRACKED_PER_PAGE 8
#define NUM_PAGES_TRACKED_PER_CORE 128
#define NUM_ENTRIES_PER_DELTA_PRED_TABLE 64

#define NUM_ENTRIES_IN_OFFSET_TABLE 64

#define ZERO_ORDER_REPL_POLICY FIFO
#define HIGHER_ORDER_REPL_POLICY FIFO

#define MAX_SAT_VALUE 4

#define NUM_PROCS 1
#define NUM_PAGE_BITS 12
#define VLDP_DEBUG 0

#define NUM_DELTA_PREDICTION_TABLES 4

#if PER_CORE_PREDICTION_TABLES == 1
#define VLDP_TABLE delta_prediction_table[cpu_id]
#else
#define VLDP_TABLE delta_prediction_table
#endif

//virtual dram queue structure
typedef struct dram_queue {
    unsigned long long int cache_line_addr;
    unsigned long long int cycle_added;
    int flag;
    struct dram_queue *next;
} dram_queue;

dram_queue *head_dram = NULL;

typedef struct prefetch_queue {
    unsigned long long int base_cache_line_addr;
    unsigned long long int prefetch_cache_line_addr;
    unsigned long long int cycle_added;
    int degree;
    struct prefetch_queue *next;
} prefetch_queue;

prefetch_queue *head_prefetch = NULL;

void insert_virtual_dram_queue(unsigned long long int, unsigned long long int, int);

int find_line_virtual_dram_queue(unsigned long long int);

int cnt_dram_pending_requests(unsigned long long int);

void insert_prefetch_queue(unsigned long long int, unsigned long long int, int);

int find_line_prefetch_queue(unsigned long long int);

void delete_line_prefetch_queue(unsigned long long int);

static unsigned long long int degree_accesses = 0;
static unsigned long long int degree_total = 0;

typedef unsigned long long int physical_address_t;
typedef unsigned long long int cycles_t;
typedef unsigned long long int uint64;
typedef unsigned int uint32;

//Tables can have different replacement policies
typedef enum {
    LRU, FIFO
} replacement_policy_t;

int wrap_around(int a, int b) {
    int ret;
    if (a >= b) {
        ret = a - b;
        assert(ret < b);
    }
    else if (a < 0) {
        ret = a + b;
        assert(ret > 0);
    }
    else if (a < b) {
        ret = a;
    }

    return ret;
}

int inc_sat(int a) {
    a++;

    if (a > MAX_SAT_VALUE) {
        a = MAX_SAT_VALUE;
    }

    return a;
}

int dec_sat(int a) {
    a--;

    if (a < 0) {
        a = 0;
    }

    return a;
}

int predicted_deltas[4];

typedef enum {
    PRED, UPDATE
} pred_or_update_t;

//delta history table
typedef struct delta_history_buffer {
    physical_address_t page_number;

    physical_address_t first_access_offset;

    long long int last_accessed_cline;

    int last_index_tracked;

    int deltas[NUM_DELTAS_TRACKED_PER_PAGE];

    int num_times_page_touched;

    //store the tag length of the last predictor
    int provider;

    //cycle touched
    cycles_t last_touched_cycle;

} delta_buffer_t;

delta_buffer_t delta_buffer[NUM_PROCS][NUM_PAGES_TRACKED_PER_CORE];
int last_page_replaced_for_core[NUM_PROCS];


//Offset Pred Table
typedef struct offset_prediction_table {
    physical_address_t offset;
    physical_address_t predicted_delta;

    int cpu_id;

    int accuracy;
    int replacement_accuracy;
    int num_used;

    int valid;
} offset_prediction_table_t;
offset_prediction_table_t offset_prediction_table[NUM_PROCS][NUM_ENTRIES_IN_OFFSET_TABLE];

//delta prediction Table
typedef struct delta_preditction_table {
    //This will depend on the number of history elements that are considered
    //We can accomodate upto 8 stide histories
    int tag[NUM_DELTA_PREDICTION_TABLES];

    int predicted_delta;

    int accuracy;

    int num_used;
    cycles_t last_used;

} delta_prediction_table_t;


#if PER_CORE_PREDICTION_TABLES == 1
delta_prediction_table_t delta_prediction_table[NUM_PROCS][NUM_DELTA_PREDICTION_TABLES][NUM_ENTRIES_PER_DELTA_PRED_TABLE];
#else
delta_prediction_table_t delta_prediction_table[NUM_DELTA_PREDICTION_TABLES][NUM_ENTRIES_PER_DELTA_PRED_TABLE];
#endif


void initialize_vldp_vars() {
#if PER_CORE_PREDICTION_TABLES == 1
    memset(delta_prediction_table, 0, sizeof(delta_prediction_table_t) * NUM_PROCS * NUM_DELTA_PREDICTION_TABLES *
                                      NUM_ENTRIES_PER_DELTA_PRED_TABLE);
#else
    memset(delta_prediction_table, 0, sizeof(delta_prediction_table_t)*NUM_DELTA_PREDICTION_TABLES*NUM_ENTRIES_PER_DELTA_PRED_TABLE);
#endif

    memset(delta_buffer, 0, sizeof(delta_buffer_t) * NUM_PROCS * NUM_PAGES_TRACKED_PER_CORE);
    memset(last_page_replaced_for_core, 0, sizeof(int) * NUM_PROCS);

}

//Offset prediction functions
void print_snapshot_offset_prediction_table() {
    printf("Offset Prediction Table\n");
    printf("%s %s %s %s %s\n", "Core", "Offset", "Stride", "Accuracy", "NumUsed");
    printf("-------------------------------------------------------\n");
    for (int p = 0; p < NUM_PROCS; p++) {
        for (int i = 0; i < NUM_ENTRIES_IN_OFFSET_TABLE; i++) {
            printf("%4d", offset_prediction_table[p][i].cpu_id);
            printf(" %6lld", offset_prediction_table[p][i].offset);
            printf(" %6lld", offset_prediction_table[p][i].predicted_delta);
            printf(" %8d", offset_prediction_table[p][i].accuracy);
            printf(" %7d", offset_prediction_table[p][i].num_used);
            printf("\n");
        }
    }
    printf("\n");
}

physical_address_t get_offset_prediction(int cpu_id, physical_address_t first_access_offset) {
    for (int i = 0; i < NUM_ENTRIES_IN_OFFSET_TABLE; i++) {
        if ((offset_prediction_table[cpu_id][i].offset == first_access_offset)) {
            assert((!offset_prediction_table[cpu_id][i].valid) ||
                   (offset_prediction_table[cpu_id][i].cpu_id == cpu_id));

            offset_prediction_table[cpu_id][i].num_used++;
            return offset_prediction_table[cpu_id][i].predicted_delta;
        }
    }

    //If it's a miss, then delta 0 means nothing
    return 0;
}

void update_offset_prediction_table(int cpu_id, int delta_buffer_entry) {
    physical_address_t first_access_offset;
    int found = 0;
    physical_address_t second_add_delta;


    first_access_offset = delta_buffer[cpu_id][delta_buffer_entry].first_access_offset;
    second_add_delta = delta_buffer[cpu_id][delta_buffer_entry].deltas[1];

    assert(second_add_delta != 0);

    for (int i = 0; (i < NUM_ENTRIES_IN_OFFSET_TABLE) && (!found); i++) {
        if ((offset_prediction_table[cpu_id][i].offset == first_access_offset)) {
            assert((!offset_prediction_table[cpu_id][i].valid) ||
                   (offset_prediction_table[cpu_id][i].cpu_id == cpu_id));

            found = 1;

            if (offset_prediction_table[cpu_id][i].predicted_delta == second_add_delta) {
                offset_prediction_table[cpu_id][i].accuracy = inc_sat(offset_prediction_table[cpu_id][i].accuracy);
                offset_prediction_table[cpu_id][i].replacement_accuracy = inc_sat(
                        offset_prediction_table[cpu_id][i].replacement_accuracy);
            }
            else {
                offset_prediction_table[cpu_id][i].accuracy = dec_sat(offset_prediction_table[cpu_id][i].accuracy);
                offset_prediction_table[cpu_id][i].replacement_accuracy = dec_sat(
                        offset_prediction_table[cpu_id][i].replacement_accuracy);
                if (offset_prediction_table[cpu_id][i].accuracy == 0) {
                    offset_prediction_table[cpu_id][i].predicted_delta = second_add_delta;
                    offset_prediction_table[cpu_id][i].replacement_accuracy = 0;
                }
            }
        }
    }

    if (!found) {
        int candidate_found = 0;
        for (int i = 0; (i < NUM_ENTRIES_IN_OFFSET_TABLE) && (!candidate_found); i++) {
            if (offset_prediction_table[cpu_id][i].replacement_accuracy == 0) {
                candidate_found = 1;

                offset_prediction_table[cpu_id][i].offset = first_access_offset;
                offset_prediction_table[cpu_id][i].predicted_delta = second_add_delta;
                offset_prediction_table[cpu_id][i].cpu_id = cpu_id;
                offset_prediction_table[cpu_id][i].replacement_accuracy = 0;
                offset_prediction_table[cpu_id][i].accuracy = 0;
                offset_prediction_table[cpu_id][i].num_used = 0;

                offset_prediction_table[cpu_id][i].valid = 1;

                break;
            }
        }

        if (!candidate_found) {
            for (int i = 0; (i < NUM_ENTRIES_IN_OFFSET_TABLE) && (!found); i++) {
                offset_prediction_table[cpu_id][i].replacement_accuracy = dec_sat(
                        offset_prediction_table[cpu_id][i].replacement_accuracy);
            }
        }
    }
}

//end- Offset prediction functions

void print_tag(int *tag) {
    int delta;
    printf("Tag:[");
    for (int i = (NUM_DELTA_PREDICTION_TABLES - 1); i >= 0; i--) {
        delta = *(tag + i);
        if (delta == 999) {
            printf("    *");
        } else {
            printf(" %4d", delta);
        }
    }
    printf("] ");
}


//The tag is an array. This function loops through the array to compare them
int is_tag_equal(int *a, int *b, int tag_len) {
    for (int i = 0; i < tag_len; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }

    return 1;
}

void assign_tag(int *a, int *b) {
    for (int i = 0; i < NUM_DELTA_PREDICTION_TABLES; i++) {
        a[i] = b[i];
    }
}

void print_snapshot_of_VLDP_prediction_tables(int clear_tables, int clear_stats) {
    //Print the delta history table
    printf("Stride_History_Per_core_Per_Page:\n");
    printf("%4s|%14s|%19s|%9s|%5s|%s\n", "Proc", "Page", "Last_Accessed_CLine", "Num_Touch", "Index", "Strides");
    for (int p = 0; p < NUM_PROCS; p++) {
        for (int page = 0; page < NUM_PAGES_TRACKED_PER_CORE; page++) {
            printf("%4d|", p);
            printf("%10lld|", delta_buffer[p][page].page_number);
            printf("%19lld|", delta_buffer[p][page].last_accessed_cline);
            printf("%9d|", delta_buffer[p][page].num_times_page_touched);
            printf("%5d|", delta_buffer[p][page].last_index_tracked);

            for (int s = 0; s < NUM_DELTAS_TRACKED_PER_PAGE; s++)
                printf("%4d ", delta_buffer[p][page].deltas[s]);

            printf("\n");
        }
    }

    //print the prefetch prediction tables

#if PER_CORE_PREDICTION_TABLES == 1
    int p = NUM_PROCS;
#else
    int p=1;
#endif

    printf("\n\nPrefetch_Prediction_Tables:\n");
    for (int cpu_id = 0; cpu_id < p; cpu_id++) {
        for (int table = 0; table < NUM_DELTA_PREDICTION_TABLES; table++) {
            printf("Table %d\n", table);
            for (int entry = 0; entry < NUM_ENTRIES_PER_DELTA_PRED_TABLE; entry++) {
                print_tag(&VLDP_TABLE[table][entry].tag[0]);
                printf("N:%5d A:%d P:%-3d\n", VLDP_TABLE[table][entry].num_used, VLDP_TABLE[table][entry].accuracy,
                       VLDP_TABLE[table][entry].predicted_delta);
            }
        }
    }

    printf("\n");
}


void reset_delta_history_buffer(int cpu_id, int index) {
    memset(&delta_buffer[cpu_id][index], 0, sizeof(delta_buffer_t));
}

int get_delta_buffer_entry(int cpu_id, physical_address_t page_number) {
    int i;
    for (i = 0; i < NUM_PAGES_TRACKED_PER_CORE; i++) {
        if (delta_buffer[cpu_id][i].page_number == page_number) {
            return i;
        }
    }

    assert(0);
    return -1;
}

//Get the last entry in the current page,cpu_id delta buffer. This will be the last prefetch prediction
int get_last_correct_delta(uint32 cpu_id, uint64 page_number) {
    int delta_buffer_entry;

    delta_buffer_entry = get_delta_buffer_entry(cpu_id, page_number);

    //Update will have run and inserted an entry into the history_buffer
    assert(delta_buffer_entry >= 0);

    //The last index tracked has been incremented after adding the last delta
    //For prediction, we need the latest deltas, for the update, we need the deltas before the latest
    int index = delta_buffer[cpu_id][delta_buffer_entry].last_index_tracked - 1;

    int last_correct_delta;
    last_correct_delta = delta_buffer[cpu_id][delta_buffer_entry].deltas[index];

    VLDP_DEBUG_MSG ("last_correct_delta:%d\n", last_correct_delta);

    return last_correct_delta;
}

//Go into the delta history buffer and create the tag from the
int *make_tag(uint32 cpu_id, uint64 page_number, uint32 tag_len, pred_or_update_t pred_or_update, int *tag,
              char *called_from) {
    assert(tag_len <= NUM_DELTA_PREDICTION_TABLES);

    VLDP_DEBUG_MSG ("#------\nCalled From %s\n", called_from);

    if (tag_len == 0) {
        return 0;
    }

    int first_index;
    int index;
    int delta_buffer_entry;

    for (int i = 0; i < NUM_DELTA_PREDICTION_TABLES; i++) {
        tag[i] = 999;
    }

    delta_buffer_entry = get_delta_buffer_entry(cpu_id, page_number);

    //Update will have run and inserted an entry into the history_buffer
    assert(delta_buffer_entry >= 0);

    //The last index tracked has been incremented after adding the last delta
    //For prediction, we need the latest deltas, for the update, we need the deltas before the latest
    first_index = wrap_around((delta_buffer[cpu_id][delta_buffer_entry].last_index_tracked - 1 - pred_or_update),
                              NUM_DELTAS_TRACKED_PER_PAGE);

    int test = (delta_buffer[cpu_id][delta_buffer_entry].last_index_tracked - 1 - pred_or_update);
    int val;
    if (test < 0) {
        val = test + NUM_DELTAS_TRACKED_PER_PAGE;
    }
    else if (test >= NUM_DELTAS_TRACKED_PER_PAGE) {
        val = test - NUM_DELTAS_TRACKED_PER_PAGE;
    }
    else if (test < NUM_DELTAS_TRACKED_PER_PAGE) {
        val = test;
    }

    VLDP_DEBUG_MSG ("last_index_tracked=%d pred_or_up=%d test=%d val=%d first_index=%d\n",
                    delta_buffer[cpu_id][delta_buffer_entry].last_index_tracked, pred_or_update, test, val,
                    first_index);
    assert(val == first_index);


    for (int i = (tag_len - 1); i >= 0; i--) {
        index = (first_index - i);

        if (index < 0) {
            index = index + NUM_DELTAS_TRACKED_PER_PAGE;
        }

        assert(index >= 0);


        //This is the tag creation, till here, we were just getting the deltas.

        tag[i] = delta_buffer[cpu_id][delta_buffer_entry].deltas[index];

        assert((127 + delta_buffer[cpu_id][delta_buffer_entry].deltas[index]) <= 256);
    }


    return &tag[0];
}

int update_delta_history_buffer(int cpu_id, physical_address_t page_number, long long int cline_address, int is_hit,
                                int cache_hit) {
    int found = 0;
    long long int delta;
    int i;

    cycles_t oldest_cycle = get_current_cycle(cpu_id);
    int oldest_page_index = 999;

    for (i = 0; (i < NUM_PAGES_TRACKED_PER_CORE) && (!found); i++) {
        if (delta_buffer[cpu_id][i].page_number == page_number) {
            delta_buffer[cpu_id][i].last_touched_cycle = get_current_cycle(cpu_id);

            found = 1;

            delta_buffer[cpu_id][i].num_times_page_touched++;

            delta = cline_address - delta_buffer[cpu_id][i].last_accessed_cline;

            if (delta == 0) {
                delta_buffer[cpu_id][i].num_times_page_touched--;
                return 0;
            }

            //The first time a page is accessed, the delta will be the cache line number, likely greater than 128
            if (delta > 64) {
                delta_buffer[cpu_id][i].deltas[delta_buffer[cpu_id][i].last_index_tracked] = 210;
            }
            else {
                delta_buffer[cpu_id][i].deltas[delta_buffer[cpu_id][i].last_index_tracked] = delta;
            }

            delta_buffer[cpu_id][i].last_accessed_cline = cline_address;

            delta_buffer[cpu_id][i].last_index_tracked++;
            delta_buffer[cpu_id][i].last_index_tracked %= NUM_DELTAS_TRACKED_PER_PAGE;

            if (delta_buffer[cpu_id][i].num_times_page_touched < NUM_DELTAS_TRACKED_PER_PAGE) {
                assert(delta_buffer[cpu_id][i].num_times_page_touched ==
                       delta_buffer[cpu_id][i].num_times_page_touched);
            }


            VLDP_DEBUG_MSG (
                    "\n%lld| Found! raw_delta=%lld delta_buffer[%d][%d].deltas[%d]=%d last_index_tracked=%d num_touch=%d\n",
                    get_current_cycle(cpu_id), delta, cpu_id, i, delta_buffer[cpu_id][i].last_index_tracked - 1,
                    delta_buffer[cpu_id][i].deltas[delta_buffer[cpu_id][i].last_index_tracked - 1],
                    delta_buffer[cpu_id][i].last_index_tracked, delta_buffer[cpu_id][i].num_times_page_touched);

        }
    }


    if (!found) {
        for (int j = 0; (j < NUM_PAGES_TRACKED_PER_CORE); j++) {
            if (delta_buffer[cpu_id][j].last_touched_cycle <= oldest_cycle) {
                oldest_cycle = delta_buffer[cpu_id][j].last_touched_cycle;
                oldest_page_index = j;
            }
        }
        assert(oldest_page_index != 999);

        i = last_page_replaced_for_core[cpu_id];

        reset_delta_history_buffer(cpu_id, i);

        delta_buffer[cpu_id][i].page_number = page_number;

        delta_buffer[cpu_id][i].last_touched_cycle = get_current_cycle(cpu_id);


        physical_address_t cline_b = cline_address;

        cline_b >>= 6;
        cline_b <<= 6;

        delta_buffer[cpu_id][i].first_access_offset = cline_address ^ cline_b;

        delta_buffer[cpu_id][i].num_times_page_touched++;

        delta = cline_address - delta_buffer[cpu_id][i].last_accessed_cline;

        assert(delta != 0);

        //The first time a page is accessed, the delta will be the cache line number, likely greater than 128

        delta_buffer[cpu_id][i].last_index_tracked = 0;
        //The first time a page is accessed, the delta will be the cache line number, likely greater than 128
        if (delta > 64) {
            delta_buffer[cpu_id][i].deltas[delta_buffer[cpu_id][i].last_index_tracked] = 220;
        }
        else {
            delta_buffer[cpu_id][i].deltas[delta_buffer[cpu_id][i].last_index_tracked] = delta;
        }

        VLDP_DEBUG_MSG ("delta=%d\n", delta_buffer[cpu_id][i].deltas[delta_buffer[cpu_id][i].last_index_tracked]);

        delta_buffer[cpu_id][i].last_accessed_cline = cline_address;

        delta_buffer[cpu_id][i].last_index_tracked++;

        delta_buffer[cpu_id][i].last_index_tracked = wrap_around(delta_buffer[cpu_id][i].last_index_tracked,
                                                                 NUM_DELTAS_TRACKED_PER_PAGE);

        last_page_replaced_for_core[cpu_id]++;
        last_page_replaced_for_core[cpu_id] = wrap_around(last_page_replaced_for_core[cpu_id],
                                                          NUM_PAGES_TRACKED_PER_CORE);

    }

    return delta;
}


/*
	 This is the improtant function that acutally does the prefetch.
	 The assumption is that the data structures have already been updated with the latest cache line access.
	 Detailed Description:
	 1) Starting with the table tracking the longest history compare tags
	 2) If there is match and the accuracy metrics meet some goal, then use it
	 3) Else go to the smaller history
	 4) Insert latest delta into the first order table in FIFO fashion


 */
int get_prefetch_delta_prediction(int cpu_id, physical_address_t page_number, long long int cline_address,
                                  pred_or_update_t pred_or_update, int current_degree) {
    int *tag = NULL;
    int tag_array[NUM_DELTA_PREDICTION_TABLES];
    int mod_tag_array[NUM_DELTA_PREDICTION_TABLES];
    int found = 0;
    int predicted_delta = 0;
    int delta_buffer_entry;
    delta_buffer_entry = get_delta_buffer_entry(cpu_id, page_number);
    int num_touched = delta_buffer[cpu_id][delta_buffer_entry].num_times_page_touched;

    int tag_length;

    if (num_touched > NUM_DELTA_PREDICTION_TABLES) {
        tag_length = NUM_DELTA_PREDICTION_TABLES;
    }
    else {
        tag_length = num_touched - 1;
    }

    while ((tag_length > 0) && !found) {

        VLDP_DEBUG_MSG("Looking for predictor match in table:%d ", tag_length);
        tag = make_tag(cpu_id, page_number, tag_length, pred_or_update, &tag_array[0],
                       "get_prefetch_delta_prediction1");

        for (int j = 0; j < tag_length; j++) {
            if (tag[j] > 200) {
                printf("\n\nERROR: ");
                print_tag(tag);
                printf("\n");
            }
            assert(tag[j] < 200);
        }


        //Step 1: Make Copy of tag
        for (int i = NUM_DELTA_PREDICTION_TABLES - 1; i >= 0; i--) {
            mod_tag_array[i] = tag_array[i];
        }


        //Step 2: Left Shift the actual tag by (current_degree -1)
        for (int i = (NUM_DELTA_PREDICTION_TABLES - 1); i >= (current_degree - 1); i--) {
            assert((i - (current_degree - 1)) >= 0);
            mod_tag_array[i] = mod_tag_array[i - (current_degree - 1)];
        }

        //Step 3: Replace the right most deltas with deltas from predicted_deltas
        for (int i = (current_degree - 2); i >= 0; i--) {
            assert(i >= 0);
            assert(((current_degree - 2) - i) >= 0);
            mod_tag_array[i] = predicted_deltas[(current_degree - 2) - i];
        }

        //Step 4: Mask out all deltas greater than tag_length
        for (int i = (NUM_DELTA_PREDICTION_TABLES - 1); i >= tag_length; i--) {
            mod_tag_array[i] = 999;
        }

        //Step 5: Replace the original tag
        tag = mod_tag_array;

        // Tag is ready ----------------------------


        for (int i = 0; i < NUM_ENTRIES_PER_DELTA_PRED_TABLE; i++) {
            if (ACCURACY_FLAG == 1) {
                if (is_tag_equal(&VLDP_TABLE[tag_length - 1][i].tag[0], tag, tag_length)) {
                    // for degree more than MIN_DEGREE, we have to look at accuracy as well.

                    if ((VLDP_TABLE[tag_length - 1][i].accuracy >= ACCURACY_THRESHOLD) || (tag_length >= 1)) {

                        predicted_delta = VLDP_TABLE[tag_length - 1][i].predicted_delta;
                        VLDP_TABLE[tag_length - 1][i].last_used = get_current_cycle(cpu_id);
                        found = 1;

                        if (current_degree == 1) {
                            VLDP_TABLE[tag_length - 1][i].num_used++;

                            //Update would have run before prefetch predtion. It will insert this cache access into history buffer.
                            assert(delta_buffer_entry >= 0);

                            delta_buffer[cpu_id][delta_buffer_entry].provider = tag_length - 1;
                        }

                        VLDP_DEBUG_MSG(" -Found. Prediction=%d Provider=%d\n", predicted_delta, tag_length - 1);

                        break;
                    }
                    else {
                        //We found the tag, but it does not have enough accuracy. But this still might be correct so if it is, accuracy needs to be increased
                        if (current_degree == 1) {
                            found = 0;
                            //Update would have run before prefetch predtion. It will insert this cache access into history buffer.
                            assert(delta_buffer_entry >= 0);
                            delta_buffer[cpu_id][delta_buffer_entry].provider = tag_length - 1;
                        }
                        return 1000;
                    }
                }

            } else {
                if (is_tag_equal(&VLDP_TABLE[tag_length - 1][i].tag[0], tag, tag_length)) {
                    //for degree more than MIN_DEGREE, we have to look at accuracy as well.
                    predicted_delta = VLDP_TABLE[tag_length - 1][i].predicted_delta;
                    found = 1;

                    if (current_degree == 1) {
                        VLDP_TABLE[tag_length - 1][i].num_used++;

                        delta_buffer_entry = get_delta_buffer_entry(cpu_id, page_number);

                        //Update would have run before prefetch predtion. It will insert this cache access into history buffer.
                        assert(delta_buffer_entry >= 0);

                        delta_buffer[cpu_id][delta_buffer_entry].provider = tag_length - 1;
                    }

                    VLDP_DEBUG_MSG(" -Found. Prediction=%d Provider=%d\n", predicted_delta, tag_length - 1);

                    break;
                }
            }
        }

        VLDP_DEBUG_MSG ("\n");

        tag_length--;

    }

    return predicted_delta;

}

/*
	 The zero order table is special compared to the rest. It is a table which tries to be a hit for every tag.
	 There is not way to do that because there are 256 different deltas possible.
 *	If push comes to shove, maybe we will make the 0 order table of 256 entries
 * LRU is another possiblility
 */
void update_zero_order_prediction_table(int cpu_id, physical_address_t page_number, long long int cline_address,
                                        int provider_found) {

    VLDP_DEBUG_MSG ("\nupdate_zero_order_prediction_table: provider_found:%d\n", provider_found);

    replacement_policy_t replacement_policy;

    replacement_policy = ZERO_ORDER_REPL_POLICY;


    //First see if it exiest, if it does, then check if the prediction was correct
    //For a correct prediction, increment correctness counter
    //For a wrong prediction, decrement counter. Replace prediciton if counter is 0
    //For a miss, use the replacement policy
    int *tag;
    int tag_array[NUM_DELTA_PREDICTION_TABLES];

    tag = make_tag(cpu_id, page_number, 1, UPDATE, &tag_array[0], "update_zero_order_prediction_table");

    for (int i = 0; i < NUM_ENTRIES_PER_DELTA_PRED_TABLE; i++) {
        if (is_tag_equal(&VLDP_TABLE[0][i].tag[0], tag, 1)) {
            VLDP_DEBUG_MSG ("Tag exixts in the 0 order buf index=%d\n", i);

            //Counters are only modified when the table is provider or provider +1
            //If order 0 is the provider, then the update has already been made.
            //The  only thing to do here is to add the tag if the tag is missing

            if (!provider_found) {
                int last_correct_delta;
                last_correct_delta = get_last_correct_delta(cpu_id, page_number);
                if (last_correct_delta == VLDP_TABLE[0][i].predicted_delta) {
                    VLDP_TABLE[0][i].accuracy = inc_sat(VLDP_TABLE[0][i].accuracy);

                    VLDP_DEBUG_MSG ("last_correct_delta:%d is the predicted delta. accuracy:%d\n", last_correct_delta,
                                    VLDP_TABLE[0][i].accuracy);
                }
                else {
                    VLDP_TABLE[0][i].accuracy = dec_sat(VLDP_TABLE[0][i].accuracy);

                    VLDP_DEBUG_MSG ("last_correct_delta:%d. Prediction %d is incorrect. accuracy:%d\n",
                                    last_correct_delta, VLDP_TABLE[0][i].predicted_delta, VLDP_TABLE[0][i].accuracy);
                    if (VLDP_TABLE[0][i].accuracy == 0) {
                        VLDP_TABLE[0][i].predicted_delta = last_correct_delta;

                        VLDP_DEBUG_MSG ("**changing prediction to:%d\n", last_correct_delta);
                    }
                }
            }
            return;
        }
    }

    VLDP_DEBUG_MSG ("***tag not found\n");

    if (replacement_policy == FIFO) {
        static int last_evicted_index = 0;

        VLDP_TABLE[0][last_evicted_index].accuracy = 0;
        VLDP_TABLE[0][last_evicted_index].predicted_delta = get_last_correct_delta(cpu_id, page_number);

        assign_tag(&VLDP_TABLE[0][last_evicted_index].tag[0], tag);

        VLDP_DEBUG_MSG("FIFO evicting 0 order table:\n");
        VLDP_DEBUG_MSG("last_evicted_index=%d predicted_delta=%d ", last_evicted_index,
                       VLDP_TABLE[0][last_evicted_index].predicted_delta);

        last_evicted_index++;
        //last_evicted_index%=NUM_ENTRIES_PER_DELTA_PRED_TABLE;
        last_evicted_index = wrap_around(last_evicted_index, NUM_ENTRIES_PER_DELTA_PRED_TABLE);
    }
    else {
        assert(0);
    }

    VLDP_DEBUG_MSG ("***Returning from update_zero_order\n");
}

/*
	 This function is called on every cache access. This fuction looks at the latest cache access and determines
	 if the prediction made during the previous cache access from the core and page are correct.
	 Following checks are performed:
	 1)update 0 order table in FIFO(?) order
	 2)If last access delta pattern is hit at this time, it can be correct or wrong
	 3)If miss, then nothing is done. The tags were inserted and then probably not promoted and then pushed out
	 4)If hit and prediction is correct, increment  correctness
	 5)If wrong, then check what is useless in the next higher order table. If anything is useless, then evict it.
	 Decrement the correctness of the provider. If correctness is 0, then change its prediction value


Details:
1) See who the provider of the core,page is
2) Update-
i)   See if the prediction still exists and is correct
ii)  If correct, increment correctness counter.

3) Allocate-
i)   If wrong, check longer tag for useless entries
ii)  If useless entries exist, then make a new entry in the longer table
 */
void update_delta_prediction_tables(int cpu_id, physical_address_t page_number, long long int cline_address) {

    VLDP_DEBUG_MSG ("\nupdate_delta_prediction_tables:\n");

    int delta_buffer_entry;
    int provider;
    int *tag;
    int tag_array[NUM_DELTA_PREDICTION_TABLES];
    int found_provider = 0;
    int provider_predicted_correctly = 0;

    delta_buffer_entry = get_delta_buffer_entry(cpu_id, page_number);

    //In case there are no providers, providers are defaulted to 0
    provider = delta_buffer[cpu_id][delta_buffer_entry].provider;

    assert(provider >= 0);
    assert(provider < NUM_DELTA_PREDICTION_TABLES);

    tag = make_tag(cpu_id, page_number, provider + 1, UPDATE, &tag_array[0], "update_delta_prediction_tables1");

    if (VLDP_DEBUG) {
        printf("\ndelta_buffer_entry=%d provides=%d \n", delta_buffer_entry, provider);
        print_tag(tag);
        printf("\n");
    }

    //Update the accuracy
    for (int i = 0; (i < NUM_ENTRIES_PER_DELTA_PRED_TABLE) && (tag != 0); i++) {
        if (is_tag_equal(&VLDP_TABLE[provider][i].tag[0], tag, provider + 1)) {
            found_provider = 1;

            int last_correct_delta;
            last_correct_delta = get_last_correct_delta(cpu_id, page_number);

            if (last_correct_delta == VLDP_TABLE[provider][i].predicted_delta) {
                VLDP_TABLE[provider][i].accuracy = inc_sat(VLDP_TABLE[provider][i].accuracy);
                provider_predicted_correctly = 1;
            }
            else {
                VLDP_TABLE[provider][i].accuracy = dec_sat(VLDP_TABLE[provider][i].accuracy);

                if (VLDP_TABLE[provider][i].accuracy == 0) {
                    VLDP_TABLE[provider][i].predicted_delta = last_correct_delta;

                    VLDP_DEBUG_MSG ("table=%d i=%d updating delta to:%d\n", provider, i,
                                    VLDP_TABLE[provider][i].predicted_delta);
                }
            }
        }
    }

    //Allocate: If the prediction by the provider is wrong, then there needs to be an allocation in the next longer table

    //An entry can be promoted to the second table only when the page has been touched 3 times..hence the last condition
    if ((found_provider || (provider == 0)) && (!provider_predicted_correctly) &&
        (provider < (NUM_DELTA_PREDICTION_TABLES - 1)) &&
        (provider <= (delta_buffer[cpu_id][delta_buffer_entry].num_times_page_touched - 4))) {
        int allocate_in;

        allocate_in = provider + 1;

        VLDP_DEBUG_MSG ("cpu_id=%d page_number=%lld tag_len=%d provider=%d num_touch=%d\n ", cpu_id, page_number,
                        allocate_in + 1, provider, delta_buffer[cpu_id][delta_buffer_entry].num_times_page_touched);
        tag = make_tag(cpu_id, page_number, allocate_in + 1, UPDATE, &tag_array[0], "update_delta_prediction_tables2");


        //First check if the tag exists, if it does, then it needs to be updated
        for (int i = 0; i < NUM_ENTRIES_PER_DELTA_PRED_TABLE; i++) {
            if (is_tag_equal(&VLDP_TABLE[allocate_in][i].tag[0], tag, allocate_in + 1)) {
                VLDP_TABLE[allocate_in][i].accuracy = dec_sat(VLDP_TABLE[allocate_in][i].accuracy);
                break;
            }
        }

        //If tag does not exist, then replace according to replacement policy
        if (HIGHER_ORDER_REPL_POLICY == FIFO) {
            static int last_evicted_index = 0;

            VLDP_TABLE[allocate_in][last_evicted_index].accuracy = 0;
            VLDP_TABLE[allocate_in][last_evicted_index].predicted_delta = get_last_correct_delta(cpu_id, page_number);

            assign_tag(&VLDP_TABLE[allocate_in][last_evicted_index].tag[0], tag);

            VLDP_DEBUG_MSG("FIFO evicting %d order table:\n", allocate_in);
            VLDP_DEBUG_MSG("last_evicted_index=%d predicted_delta=%d ", last_evicted_index,
                           VLDP_TABLE[allocate_in][last_evicted_index].predicted_delta);
            if (VLDP_DEBUG) {
                print_tag(tag);
                printf("\n");
            }

            last_evicted_index++;
            //last_evicted_index%=NUM_ENTRIES_PER_DELTA_PRED_TABLE;
            last_evicted_index = wrap_around(last_evicted_index, NUM_ENTRIES_PER_DELTA_PRED_TABLE);

        }
        else if (HIGHER_ORDER_REPL_POLICY == LRU) {
            cycles_t lru_cycle = get_current_cycle(cpu_id);
            int lru_pos = NUM_ENTRIES_PER_DELTA_PRED_TABLE + 1;

            for (int i = 0; (i < NUM_ENTRIES_PER_DELTA_PRED_TABLE); i++) {
                if (VLDP_TABLE[allocate_in][i].last_used < lru_cycle) {
                    lru_pos = i;
                }
            }
            assert(lru_pos < NUM_ENTRIES_PER_DELTA_PRED_TABLE);

            assign_tag(&VLDP_TABLE[allocate_in][lru_pos].tag[0], tag);
            VLDP_TABLE[allocate_in][lru_pos].accuracy = 0;
            VLDP_TABLE[allocate_in][lru_pos].predicted_delta = get_last_correct_delta(cpu_id, page_number);
            VLDP_TABLE[allocate_in][lru_pos].num_used = 0;
            VLDP_TABLE[allocate_in][lru_pos].last_used = 0;
        }
        else {
            assert(0);
        }
    }

    //-end allocate




    //This is update last because mispredicts made by 0 order will need to to propagate upwards.
    //if the provider is not found, then if the tag exists in 0 order, then the prediction might need to be updated
    //if the provider is found, and if it was 0 order, all the updates would have already happened.
    update_zero_order_prediction_table(cpu_id, page_number, cline_address, found_provider);

}


void schedule_prefetch_to_dram() {


    int l2_read_queue_level;
    int l2_mshr_queue_level;

    unsigned long long int page_prefetch_addr;
    unsigned long long int prefetch_addr;
    unsigned long long int prefetch_line;
    unsigned long long int base_addr;
    prefetch_queue *prefetch_el, *prefetch_temp;


    //delete old requests ...
    LL_FOREACH_SAFE(head_prefetch, prefetch_el, prefetch_temp) {

        if ((get_current_cycle(0) - (prefetch_el->cycle_added)) > 2000) {
            LL_DELETE(head_prefetch, prefetch_el);
            free(prefetch_el);
        }
    }

    int cnt = 0;
    //find the oldest thing with least degree from prefetch queue, and try to issue it depending on the follwoing two conditions. . .

    LL_FOREACH_SAFE(head_prefetch, prefetch_el, prefetch_temp) {

        page_prefetch_addr = (prefetch_el->prefetch_cache_line_addr) >> 6;

        if ((cnt_dram_pending_requests(page_prefetch_addr) <= ROW_HIT_PER_PAGE)) {

            //if request is too old don't issue it, delete it from prefetch queue ...
            if (((prefetch_el->degree) > MAX_DEGREE_PREFETCHED)) {
                LL_DELETE(head_prefetch, prefetch_el);
                free(prefetch_el);
            } else {
                cnt++;

                prefetch_addr = (prefetch_el->prefetch_cache_line_addr) << 6;
                prefetch_line = (prefetch_el->prefetch_cache_line_addr);
                base_addr = (prefetch_el->base_cache_line_addr) << 6;

                l2_read_queue_level = get_l2_read_queue_occupancy(0);
                l2_mshr_queue_level = get_l2_mshr_occupancy(0);

                //prefetch to l2
                if ((l2_read_queue_level < (0.75 * L2_READ_QUEUE_SIZE)) &&
                    (l2_mshr_queue_level < (0.75 * L2_MSHR_COUNT))) {
                    if (l2_prefetch_line(0, base_addr, prefetch_addr, FILL_L2) == 1) {
                        //insert to dram queue
                        if (find_line_virtual_dram_queue(prefetch_line) == 0) {
                            //l2 level ...
                            insert_virtual_dram_queue(prefetch_line, get_current_cycle(0), 2);
                        }
                    }
                }
                    //prefetch to l3
                else {
                    if (l2_prefetch_line(0, base_addr, prefetch_addr, FILL_LLC) == 1) {
                        //insert to dram queue
                        if (find_line_virtual_dram_queue(prefetch_line) == 0) {
                            //l2 level ...
                            insert_virtual_dram_queue(prefetch_line, get_current_cycle(0), 3);
                        }
                    }
                }

                LL_DELETE(head_prefetch, prefetch_el);
                free(prefetch_el);

                if (cnt > CACHE_FILL_PREFETCH_THRESHOLD) {
                    break;
                }

            }
        }
    }
}

void insert_virtual_dram_queue(unsigned long long int cache_line_addr, unsigned long long int cycle_added, int flag) {

    //only 100 elements can be in queue ...
    dram_queue *el;
    int count;
    LL_COUNT(head_dram, el, count);

    if (count >= DRAM_QUEUE) {
        return;
    }

    if (MAX_DEGREE == 2) {

        if ((count >= 0) && (count < DEGREE1_THRESHOLD)) {
            MAX_DEGREE_PREFETCHED = 2;

        } else if ((count >= DEGREE1_THRESHOLD)) {
            MAX_DEGREE_PREFETCHED = 1;
        }
    }

    if (MAX_DEGREE == 3) {

        if ((count >= 0) && (count < DEGREE1_THRESHOLD)) {
            MAX_DEGREE_PREFETCHED = 3;

        } else if ((count >= DEGREE1_THRESHOLD) && (count < DEGREE2_THRESHOLD)) {
            MAX_DEGREE_PREFETCHED = 2;

        } else if ((count >= DEGREE2_THRESHOLD)) {
            MAX_DEGREE_PREFETCHED = 1;

        }
    }

    if (MAX_DEGREE == 4) {

        if ((count >= 0) && (count < DEGREE1_THRESHOLD)) {
            MAX_DEGREE_PREFETCHED = 4;

        } else if ((count >= DEGREE1_THRESHOLD) && (count < DEGREE2_THRESHOLD)) {
            MAX_DEGREE_PREFETCHED = 3;

        } else if ((count >= DEGREE2_THRESHOLD) && (count < DEGREE3_THRESHOLD)) {
            MAX_DEGREE_PREFETCHED = 2;

        } else if ((count >= DEGREE3_THRESHOLD)) {
            MAX_DEGREE_PREFETCHED = 1;
        }
    }


    assert(count < DRAM_QUEUE);

    dram_queue *element = (dram_queue *) malloc(sizeof(dram_queue));

    if (!element) {
        printf("can't malloc at dram queue...");
        exit(-1);
    }

    element->cache_line_addr = cache_line_addr;
    element->cycle_added = cycle_added;
    element->flag = flag;

    LL_APPEND(head_dram, element);
}

int find_line_virtual_dram_queue(unsigned long long int cache_line) {

    dram_queue *el;
    int flag = 0;

    LL_FOREACH(head_dram, el) {
        //line found
        if ((el->cache_line_addr) == cache_line) {
            flag = 1;
            break;
        }
    }

    if (flag == 1) {
        return 1;
    } else {
        return 0;
    }
}

int cnt_dram_pending_requests(unsigned long long int page_addr) {

    unsigned long long int page_address;

    dram_queue *el;

    int cnt = 0;

    LL_FOREACH(head_dram, el) {
        page_address = (el->cache_line_addr) >> 6;
        if (page_address == page_addr) {
            cnt++;
        }
    }

    return cnt;
}

//prefetch queue ...
void insert_prefetch_queue(unsigned long long int base_cache_line_addr, unsigned long long int prefetch_cache_line_addr,
                           int degree) {

    //only 100 elements can be in queue ...
    prefetch_queue *el;
    int count;
    LL_COUNT(head_prefetch, el, count);

    if (count >= PREFETCH_QUEUE) {
        return;
    }

    prefetch_queue *element = (prefetch_queue *) malloc(sizeof(prefetch_queue));

    if (!element) {
        printf("can't malloc at prefetch queue...");
        exit(-1);
    }

    element->prefetch_cache_line_addr = prefetch_cache_line_addr;
    element->base_cache_line_addr = base_cache_line_addr;
    element->cycle_added = get_current_cycle(0);
    element->degree = degree;

    LL_APPEND(head_prefetch, element);
}

void update_degree_prefetch_queue(unsigned long long int cache_line_addr, int degree) {

    prefetch_queue *el;

    LL_FOREACH(head_prefetch, el) {
        //line found
        if ((el->prefetch_cache_line_addr) == cache_line_addr) {
            el->degree = degree;
            break;
        }
    }
}

int find_line_prefetch_queue(unsigned long long int cache_line_addr) {

    prefetch_queue *el;
    int flag = 0;

    LL_FOREACH(head_prefetch, el) {
        //line found
        if ((el->prefetch_cache_line_addr) == cache_line_addr) {
            flag = 1;
            break;
        }
    }

    if (flag == 1) {
        return 1;
    } else {
        return 0;
    }
}

void delete_line_prefetch_queue(unsigned long long int cache_line_addr) {

    prefetch_queue *el, *temp;
    LL_FOREACH_SAFE(head_prefetch, el, temp) {
        if ((el->prefetch_cache_line_addr) == cache_line_addr) {
            LL_DELETE(head_prefetch, el);
            free(el);
        }
    }
}

// Hybrid Prefetcher code

void l2_prefetcher_initialize(int cpu_num) {
    printf("No Prefetching\n");
    // you can inspect these knob values from your code to see which configuration you're runnig in
    printf("Knobs visible from prefetcher: %d %d %d\n", knob_scramble_loads, knob_small_llc, knob_low_bandwidth);
}

void l2_prefetcher_operate(int cpu_num, unsigned long long int addr, unsigned long long int ip, int cache_hit) {
    // uncomment this line to see all the information available to make prefetch decisions
    //printf("(0x%llx 0x%llx %d %d %d) ", addr, ip, cache_hit, get_l2_read_queue_occupancy(0), get_l2_mshr_occupancy(0));
}

void l2_cache_fill(int cpu_num, unsigned long long int addr, int set, int way, int prefetch,
                   unsigned long long int evicted_addr) {
    // uncomment this line to see the information available to you when there is a cache fill event
    //printf("0x%llx %d %d %d 0x%llx\n", addr, set, way, prefetch, evicted_addr);
}

void l2_prefetcher_heartbeat_stats(int cpu_num) {
    printf("Prefetcher heartbeat stats\n");
}

void l2_prefetcher_warmup_stats(int cpu_num) {
    printf("Prefetcher warmup complete stats\n\n");
}

void l2_prefetcher_final_stats(int cpu_num) {
    printf("Prefetcher final stats\n");
}