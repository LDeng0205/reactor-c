#define LOG_LEVEL 2
#include "target/ctarget.h"
#define NUMBER_OF_FEDERATES 1
#define TARGET_FILES_DIRECTORY "/Users/arthurdeng/Desktop/LFArduino/lfcode/src-gen/helloworld"
#include "core/reactor.c"
// Code generated by the Lingua Franca compiler from:
// file://Users/arthurdeng/Desktop/LFArduino/lfcode/helloworld.lf
// =============== START reactor class helloworld
typedef struct {
    reaction_t _lf__reaction_0;
    trigger_t _lf__startup;
    reaction_t* _lf__startup_reactions[1];
} helloworld_self_t;
void helloworldreaction_function_0(void* instance_args) {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-variable"
    helloworld_self_t* self = (helloworld_self_t*)instance_args;
    
    #pragma GCC diagnostic pop
    int x;
    x++;
       
}
helloworld_self_t* new_helloworld() {
    helloworld_self_t* self = (helloworld_self_t*)calloc(1, sizeof(helloworld_self_t));
    self->_lf__reaction_0.number = 0;
    self->_lf__reaction_0.function = helloworldreaction_function_0;
    self->_lf__reaction_0.self = self;
    self->_lf__reaction_0.deadline_violation_handler = NULL;
    self->_lf__reaction_0.STP_handler = NULL;
    self->_lf__reaction_0.name = "?";
    self->_lf__startup_reactions[0] = &self->_lf__reaction_0;
    self->_lf__startup.last = NULL;
    self->_lf__startup.reactions = &self->_lf__startup_reactions[0];
    self->_lf__startup.number_of_reactions = 1;
    self->_lf__startup.is_timer = false;
    return self;
}
void delete_helloworld(helloworld_self_t* self) {
    if (self->_lf__reaction_0.output_produced != NULL) {
        free(self->_lf__reaction_0.output_produced);
    }
    if (self->_lf__reaction_0.triggers != NULL) {
        free(self->_lf__reaction_0.triggers);
    }
    if (self->_lf__reaction_0.triggered_sizes != NULL) {
        free(self->_lf__reaction_0.triggered_sizes);
    }
    for(int i = 0; i < self->_lf__reaction_0.num_outputs; i++) {
        free(self->_lf__reaction_0.triggers[i]);
    }
    free(self);
}
// =============== END reactor class helloworld

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
    
    // ************* Instance helloworld of class helloworld
    helloworld_self_t* helloworld_self = new_helloworld();
    //***** Start initializing helloworld
    _lf_startup_reactions[0] = &helloworld_self->_lf__reaction_0;
    //***** End initializing helloworld
    // Populate arrays of trigger pointers.
    // Total number of outputs produced by the reaction.
    helloworld_self->_lf__reaction_0.num_outputs = 0;
    // Allocate arrays for triggering downstream reactions.
    if (helloworld_self->_lf__reaction_0.num_outputs > 0) {
        helloworld_self->_lf__reaction_0.output_produced 
                = (bool**)malloc(sizeof(bool*) * helloworld_self->_lf__reaction_0.num_outputs);
        helloworld_self->_lf__reaction_0.triggers 
                = (trigger_t***)malloc(sizeof(trigger_t**) * helloworld_self->_lf__reaction_0.num_outputs);
        helloworld_self->_lf__reaction_0.triggered_sizes 
                = (int*)calloc(helloworld_self->_lf__reaction_0.num_outputs, sizeof(int));
    }
    // Initialize the output_produced array.
    // Reaction 0 of helloworld does not depend on one maximal upstream reaction.
    helloworld_self->_lf__reaction_0.last_enabling_reaction = NULL;
    // doDeferredInitialize
    // Connect inputs and outputs for reactor helloworld.
    // END Connect inputs and outputs for reactor helloworld.
    
    helloworld_self->_lf__reaction_0.chain_id = 1;
    // index is the OR of level 0 and 
    // deadline 140737488355327 shifted left 16 bits.
    helloworld_self->_lf__reaction_0.index = 0x7fffffffffff0000LL;
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
