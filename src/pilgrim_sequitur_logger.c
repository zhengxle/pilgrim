#include <stdio.h>
#include <stdlib.h>
#include "pilgrim_sequitur.h"
#include "mpi.h"


/**
 * Store the Grammer in an integer array
 *
 * | #rules |
 * | rule 1 head | #symbols of rule 1 | symbol 1, ..., symbol N |
 * | rule 2 head | #symbols of rule 2 | symbol 1, ..., symbol N |
 * ...
 *
 * @len: [out] the length of the array: 1 + 2 * number of rules + number of symbols
 * @return: return the array, need to be freed by the caller
 *
 */
int* serialize_grammar(Grammar *grammar, int* update_terminal_id, size_t *len) {

    int total_integers = 1, symbols_count = 0, rules_count = 0;

    Symbol *rule, *sym;
    DL_COUNT(grammar->rules, rule, rules_count);

    total_integers += 2 * rules_count;

    DL_FOREACH(grammar->rules, rule) {
        DL_COUNT(rule->rule_body, sym, symbols_count);
        total_integers += symbols_count;
    }

    int i = 0;
    int *data = mymalloc(sizeof(int) * total_integers);
    data[i++]  = rules_count;
    DL_FOREACH(grammar->rules, rule) {
        DL_COUNT(rule->rule_body, sym, symbols_count);
        data[i++] = rule->val;
        data[i++] = symbols_count;

        DL_FOREACH(rule->rule_body, sym) {
            if(sym->val >= 0 && update_terminal_id)
                data[i++] = update_terminal_id[sym->val];   // terminal id is updated according to the compressed function table
            else
                data[i++] = sym->val;       // rule id does not change
        }

    }

    *len = total_integers;
    return data;
}


/**
 * Use MPI to gather grammars from all ranks
 *
 * @total_len: output parameter, is length of the returned grammar (interger array)
 * @return: gathered grammars in a 1D integer array
 */
#include "uthash.h"
struct GrammarSize {
    int size;
    int count;
    int rank;
    UT_hash_handle hh;
};
struct GrammarSize *gs_table = NULL;
int* gather_grammars(Grammar *grammar, int* update_terminal_id, int mpi_rank, int mpi_size, size_t* len_sum) {
    size_t len = 0;
    int *local_grammar = serialize_grammar(grammar, update_terminal_id, &len);

    int recvcounts[mpi_size], displs[mpi_size];
    PMPI_Gather(&len, 1, MPI_INT, recvcounts, 1, MPI_INT, 0, MPI_COMM_WORLD);

    displs[0] = 0;
    *len_sum = recvcounts[0];
    for(int i = 1; i < mpi_size;i++) {
        *len_sum += recvcounts[i];
        displs[i] = displs[i-1] + recvcounts[i-1];

        // TODO: remove this if when finsihed debuging
        /*
        if(mpi_rank ==0) {
            struct GrammarSize *gs_entry = NULL;
            HASH_FIND_INT(gs_table, &(recvcounts[i]), gs_entry);
            if(gs_entry) {
                gs_entry->count++;
            } else {
                gs_entry = dlmalloc(sizeof(struct GrammarSize));
                gs_entry->size = recvcounts[i];
                gs_entry->count = 1;
                gs_entry->rank = i;
                HASH_ADD_INT(gs_table, size, gs_entry);
            }
        }
        */
    }
    // TODO: remove this when finsihed debuging
    /*
    struct GrammarSize *s, *tmp;
    if(mpi_rank == 0)
        printf("Rank: 0, Size: %d, Count: 1\n", recvcounts[0]);
    HASH_ITER(hh, gs_table, s, tmp) {
        printf("Rank: %d, Size: %d, Count: %d\n", s->rank, s->size, s->count);
        HASH_DEL(gs_table, s);
        dlfree(s);
    }
    */

    int *gathered_grammars = NULL;
    if(mpi_rank == 0)
        gathered_grammars = mymalloc(sizeof(int) * (*len_sum));

    PMPI_Gatherv(local_grammar, len, MPI_INT, gathered_grammars, recvcounts, displs, MPI_INT, 0, MPI_COMM_WORLD);

    myfree(local_grammar, len);
    return gathered_grammars;
}

int* sequitur_dump(const char* path, Grammar *grammar, int* update_terminal_id, int mpi_rank, int mpi_size, size_t* outlen) {
    // gathered_grammars is NULL except rank 0
    size_t len;
    int *gathered_grammars = gather_grammars(grammar, update_terminal_id, mpi_rank, mpi_size, &len);
    if( mpi_rank != 0)
        return gathered_grammars;

    FILE* f = fopen(path, "wb");
    fwrite(gathered_grammars, sizeof(int), len, f);
    fclose(f);

    //myfree(gathered_grammars, len);
    *outlen = len;
    return gathered_grammars;
}
