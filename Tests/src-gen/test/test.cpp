#define LOG_LEVEL 2
#include "ctarget.h"
#define NUMBER_OF_FEDERATES 1
#define TARGET_FILES_DIRECTORY "/Users/arthurdeng/Desktop/LFArduino/reactor-c/Tests/src-gen/test"
#include "core/reactor.c"
// Code generated by the Lingua Franca compiler from:
// file://Users/arthurdeng/Desktop/LFArduino/reactor-c/Tests/test.lf
// =============== START reactor class test
typedef struct {
    trigger_t* trigger;
    bool is_present;
    bool has_value;
    lf_token_t* token;
} test_loop_t;
typedef struct {
    test_loop_t _lf_loop;
    reaction_t _lf__reaction_0;
    reaction_t _lf__reaction_1;
    trigger_t _lf__startup;
    reaction_t* _lf__startup_reactions[1];
    trigger_t _lf__loop;
    reaction_t* _lf__loop_reactions[1];
} test_self_t;
void testreaction_function_0(void* instance_args) {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-variable"
    test_self_t* self = (test_self_t*)instance_args;
    
    #pragma GCC diagnostic pop
    printf("Hello world!\n");
    schedule(loop, 0);
        
}
void testreaction_function_1(void* instance_args) {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-variable"
    test_self_t* self = (test_self_t*)instance_args;
    // Expose the action struct as a local variable whose name matches the action name.
    test_loop_t* loop = &self->_lf_loop;
    // Set the fields of the action struct to match the current trigger.
    loop->is_present = (bool)self->_lf__loop.status;
    loop->has_value = ((self->_lf__loop.token) != NULL && (self->_lf__loop.token)->value != NULL);
    loop->token = (self->_lf__loop.token);
    #pragma GCC diagnostic pop
    printf("Hello world!\n");
    schedule(loop, 0);
        
}
test_self_t* new_test() {
    test_self_t* self = (test_self_t*)calloc(1, sizeof(test_self_t));
    self->_lf_loop.trigger = &self->_lf__loop;
    self->_lf__reaction_0.number = 0;
    self->_lf__reaction_0.function = testreaction_function_0;
    self->_lf__reaction_0.self = self;
    self->_lf__reaction_0.deadline_violation_handler = NULL;
    self->_lf__reaction_0.STP_handler = NULL;
    self->_lf__reaction_0.name = "?";
    self->_lf__reaction_1.number = 1;
    self->_lf__reaction_1.function = testreaction_function_1;
    self->_lf__reaction_1.self = self;
    self->_lf__reaction_1.deadline_violation_handler = NULL;
    self->_lf__reaction_1.STP_handler = NULL;
    self->_lf__reaction_1.name = "?";
    self->_lf__startup_reactions[0] = &self->_lf__reaction_0;
    self->_lf__startup.last = NULL;
    self->_lf__startup.reactions = &self->_lf__startup_reactions[0];
    self->_lf__startup.number_of_reactions = 1;
    self->_lf__startup.is_timer = false;
    self->_lf__loop.last = NULL;
    self->_lf__loop_reactions[0] = &self->_lf__reaction_1;
    self->_lf__loop.reactions = &self->_lf__loop_reactions[0];
    self->_lf__loop.number_of_reactions = 1;
    self->_lf__loop.is_physical = false;
    self->_lf__loop.element_size = 0;
    return self;
}
void delete_test(test_self_t* self) {
    if (self->_lf__reaction_0.output_produced != NULL) {
        free(self->_lf__reaction_0.output_produced);
    }
    if (self->_lf__reaction_0.triggers != NULL) {
        free(self->_lf__reaction_0.triggers);
    }
    if (self->_lf__reaction_0.triggered_sizes != NULL) {
        free(self->_lf__reaction_0.triggered_sizes);
    }
    if (self->_lf__reaction_1.output_produced != NULL) {
        free(self->_lf__reaction_1.output_produced);
    }
    if (self->_lf__reaction_1.triggers != NULL) {
        free(self->_lf__reaction_1.triggers);
    }
    if (self->_lf__reaction_1.triggered_sizes != NULL) {
        free(self->_lf__reaction_1.triggered_sizes);
    }
    for(int i = 0; i < self->_lf__reaction_0.num_outputs; i++) {
        free(self->_lf__reaction_0.triggers[i]);
    }
    for(int i = 0; i < self->_lf__reaction_1.num_outputs; i++) {
        free(self->_lf__reaction_1.triggers[i]);
    }
    free(self);
}
// =============== END reactor class test

void _lf_set_default_command_line_options() {
}
// Array of pointers to timer triggers to be scheduled in _lf_initialize_timers().
trigger_t** _lf_timer_triggers = NULL;
int _lf_timer_triggers_size = 0;
// Array of pointers to timer triggers to be scheduled in _lf_trigger_startup_reactions().
reaction_t* _lf_startup_reactions[1];
int _lf_startup_reactions_size = 1;
// Empty array of pointers to shutdown triggers.
reaction_t** _lf_shutdown_reactions = NULL;
int _lf_shutdown_reactions_size = 0;
trigger_t* _lf_action_for_port(int port_id) {
    return NULL;
}
void _lf_initialize_trigger_objects() {
    // Initialize the _lf_clock
    lf_initialize_clock();
    _lf_tokens_with_ref_count_size = 1;
    _lf_tokens_with_ref_count = (token_present_t*)malloc(1 * sizeof(token_present_t));
    // Create the array that will contain pointers to is_present fields to reset on each step.
    _lf_is_present_fields_size = 1;
    _lf_is_present_fields = (bool**)malloc(1 * sizeof(bool*));
    _lf_is_present_fields_abbreviated = (bool**)malloc(1 * sizeof(bool*));
    _lf_is_present_fields_abbreviated_size = 0;
    
    test_self_t* test_self = new_test();
    _lf_startup_reactions[0] = &test_self->_lf__reaction_0;
    test_self->_lf__loop.offset = 0;
    test_self->_lf__loop.period = -1;
    test_self->_lf__loop.token = _lf_create_token(0);
    test_self->_lf__loop.status = absent;
    _lf_tokens_with_ref_count[0].token
            = &test_self->_lf__loop.token;
    _lf_tokens_with_ref_count[0].status
            = &test_self->_lf__loop.status;
    _lf_tokens_with_ref_count[0].reset_is_present = true;
    //***** End initializing test
    // Allocate memory.
    // Populate arrays of trigger pointers.
    // Total number of outputs (single ports and multiport channels) produced by the reaction.
    test_self->_lf__reaction_0.num_outputs = 0;
    // Allocate arrays for triggering downstream reactions.
    if (test_self->_lf__reaction_0.num_outputs > 0) {
        test_self->_lf__reaction_0.output_produced 
                = (bool**)malloc(sizeof(bool*) * test_self->_lf__reaction_0.num_outputs);
        test_self->_lf__reaction_0.triggers 
                = (trigger_t***)malloc(sizeof(trigger_t**) * test_self->_lf__reaction_0.num_outputs);
        test_self->_lf__reaction_0.triggered_sizes 
                = (int*)calloc(test_self->_lf__reaction_0.num_outputs, sizeof(int));
    }
    // Initialize the output_produced array.
    // Total number of outputs (single ports and multiport channels) produced by the reaction.
    test_self->_lf__reaction_1.num_outputs = 0;
    // Allocate arrays for triggering downstream reactions.
    if (test_self->_lf__reaction_1.num_outputs > 0) {
        test_self->_lf__reaction_1.output_produced 
                = (bool**)malloc(sizeof(bool*) * test_self->_lf__reaction_1.num_outputs);
        test_self->_lf__reaction_1.triggers 
                = (trigger_t***)malloc(sizeof(trigger_t**) * test_self->_lf__reaction_1.num_outputs);
        test_self->_lf__reaction_1.triggered_sizes 
                = (int*)calloc(test_self->_lf__reaction_1.num_outputs, sizeof(int));
    }
    // Initialize the output_produced array.
    // Reaction 0 of test does not depend on one maximal upstream reaction.
    test_self->_lf__reaction_0.last_enabling_reaction = NULL;
    // Reaction 1 of test depends on one maximal upstream reaction.
    test_self->_lf__reaction_1.last_enabling_reaction = &(test_self->_lf__reaction_0);
    // doDeferredInitialize
    // Connect inputs and outputs for reactor test.
    // END Connect inputs and outputs for reactor test.
    // Add action test.loop to array of is_present fields.
    _lf_is_present_fields[0] 
            = &test_self->_lf_loop.is_present;
    test_self->_lf__reaction_0.chain_id = 1;
    // index is the OR of level 0 and 
    // deadline 140737488355327 shifted left 16 bits.
    test_self->_lf__reaction_0.index = 0x7fffffffffff0000LL;
    test_self->_lf__reaction_1.chain_id = 1;
    // index is the OR of level 1 and 
    // deadline 140737488355327 shifted left 16 bits.
    test_self->_lf__reaction_1.index = 0x7fffffffffff0001LL;
}
void _lf_trigger_startup_reactions() {
    
    for (int i = 0; i < _lf_startup_reactions_size; i++) {
        if (_lf_startup_reactions[i] != NULL) {
            _lf_enqueue_reaction(_lf_startup_reactions[i]);
        }
    }
}
void _lf_initialize_timers() {
}
void logical_tag_complete(tag_t tag_to_send) {
}
bool _lf_trigger_shutdown_reactions() {                          
    for (int i = 0; i < _lf_shutdown_reactions_size; i++) {
        if (_lf_shutdown_reactions[i] != NULL) {
            _lf_enqueue_reaction(_lf_shutdown_reactions[i]);
        }
    }
    // Return true if there are shutdown reactions.
    return (_lf_shutdown_reactions_size > 0);
}
void terminate_execution() {}
