#define LOG_LEVEL 2
#include "ctarget.h"
#define NUMBER_OF_FEDERATES 1
#define TARGET_FILES_DIRECTORY "D:\Documents\GitHub\Lingua Franca Website\lingua-franca\example\C\src-gen\Test\Blink"
#include "core/reactor.c"
// Code generated by the Lingua Franca compiler from:
// file:/D:/Documents/GitHub/Lingua Franca Website/lingua-franca/example/C/src/Test/Blink.lf
#include <Arduino.h>
// =============== START reactor class Blink
typedef struct {
    reaction_t _lf__reaction_0;
    reaction_t _lf__reaction_1;
    reaction_t _lf__reaction_2;
    trigger_t _lf__t1;
    reaction_t* _lf__t1_reactions[1];
    trigger_t _lf__t2;
    reaction_t* _lf__t2_reactions[1];
    trigger_t _lf__startup;
    reaction_t* _lf__startup_reactions[1];
} blink_self_t;

#ifdef __arm__
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char* sbrk(int incr);
#else  // __ARM__
extern char *__brkval;
#endif  // __arm__

int freeMemory() {
  char top;
#ifdef __arm__
  return &top - reinterpret_cast<char*>(sbrk(0));
#elif defined(CORE_TEENSY) || (ARDUINO > 103 && ARDUINO != 151)
  return &top - __brkval;
#else  // __arm__
  return __brkval ? &top - __brkval : &top - __malloc_heap_start;
#endif  // __arm__
}

void blinkreaction_function_0(void* instance_args) {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-variable"
    blink_self_t* self = (blink_self_t*)instance_args;
    
    #pragma GCC diagnostic pop
    Serial.begin(9600);
    pinMode(LED_BUILTIN, OUTPUT);
        
}
void blinkreaction_function_1(void* instance_args) {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-variable"
    blink_self_t* self = (blink_self_t*)instance_args;
    
    #pragma GCC diagnostic pop
    Serial.print("switch high\n");
    Serial.println(freeMemory());
    digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
        
}
void blinkreaction_function_2(void* instance_args) {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-variable"
    blink_self_t* self = (blink_self_t*)instance_args;
    
    #pragma GCC diagnostic pop
    Serial.print("switch low\n");
    Serial.println(freeMemory());
    digitalWrite(LED_BUILTIN, LOW);   // turn the LED off (LOW is the voltage level)
        
}
blink_self_t* new_Blink() {
    blink_self_t* self = (blink_self_t*)calloc(1, sizeof(blink_self_t));
    self->_lf__reaction_0.number = 0;
    self->_lf__reaction_0.function = blinkreaction_function_0;
    self->_lf__reaction_0.self = self;
    self->_lf__reaction_0.deadline_violation_handler = NULL;
    self->_lf__reaction_0.STP_handler = NULL;
    self->_lf__reaction_0.name = "?";
    self->_lf__reaction_1.number = 1;
    self->_lf__reaction_1.function = blinkreaction_function_1;
    self->_lf__reaction_1.self = self;
    self->_lf__reaction_1.deadline_violation_handler = NULL;
    self->_lf__reaction_1.STP_handler = NULL;
    self->_lf__reaction_1.name = "?";
    self->_lf__reaction_2.number = 2;
    self->_lf__reaction_2.function = blinkreaction_function_2;
    self->_lf__reaction_2.self = self;
    self->_lf__reaction_2.deadline_violation_handler = NULL;
    self->_lf__reaction_2.STP_handler = NULL;
    self->_lf__reaction_2.name = "?";
    self->_lf__t1.last = NULL;
    self->_lf__t1_reactions[0] = &self->_lf__reaction_1;
    self->_lf__t1.reactions = &self->_lf__t1_reactions[0];
    self->_lf__t1.number_of_reactions = 1;
    self->_lf__t1.is_timer = true;
    self->_lf__t2.last = NULL;
    self->_lf__t2_reactions[0] = &self->_lf__reaction_2;
    self->_lf__t2.reactions = &self->_lf__t2_reactions[0];
    self->_lf__t2.number_of_reactions = 1;
    self->_lf__t2.is_timer = true;
    self->_lf__startup_reactions[0] = &self->_lf__reaction_0;
    self->_lf__startup.last = NULL;
    self->_lf__startup.reactions = &self->_lf__startup_reactions[0];
    self->_lf__startup.number_of_reactions = 1;
    self->_lf__startup.is_timer = false;
    return self;
}
void delete_Blink(blink_self_t* self) {
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
    if (self->_lf__reaction_2.output_produced != NULL) {
        free(self->_lf__reaction_2.output_produced);
    }
    if (self->_lf__reaction_2.triggers != NULL) {
        free(self->_lf__reaction_2.triggers);
    }
    if (self->_lf__reaction_2.triggered_sizes != NULL) {
        free(self->_lf__reaction_2.triggered_sizes);
    }
    for(int i = 0; i < self->_lf__reaction_0.num_outputs; i++) {
        free(self->_lf__reaction_0.triggers[i]);
    }
    for(int i = 0; i < self->_lf__reaction_1.num_outputs; i++) {
        free(self->_lf__reaction_1.triggers[i]);
    }
    for(int i = 0; i < self->_lf__reaction_2.num_outputs; i++) {
        free(self->_lf__reaction_2.triggers[i]);
    }
    free(self);
}
// =============== END reactor class Blink

void _lf_set_default_command_line_options() {
}
// Array of pointers to timer triggers to be scheduled in _lf_initialize_timers().
trigger_t* _lf_timer_triggers[2];
int _lf_timer_triggers_size = 2;
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
    
    // ************* Instance Blink of class Blink
    blink_self_t* blink_self = new_Blink();
    //***** Start initializing Blink
    _lf_startup_reactions[0] = &blink_self->_lf__reaction_0;
    blink_self->_lf__t1.offset = 0;
    blink_self->_lf__t1.period = SEC(1);
    _lf_timer_triggers[0] = &blink_self->_lf__t1;
    blink_self->_lf__t2.offset = MSEC(500);
    blink_self->_lf__t2.period = SEC(1);
    _lf_timer_triggers[1] = &blink_self->_lf__t2;
    //***** End initializing Blink
    // Populate arrays of trigger pointers.
    // Total number of outputs produced by the reaction.
    blink_self->_lf__reaction_0.num_outputs = 0;
    // Allocate arrays for triggering downstream reactions.
    if (blink_self->_lf__reaction_0.num_outputs > 0) {
        blink_self->_lf__reaction_0.output_produced 
                = (bool**)malloc(sizeof(bool*) * blink_self->_lf__reaction_0.num_outputs);
        blink_self->_lf__reaction_0.triggers 
                = (trigger_t***)malloc(sizeof(trigger_t**) * blink_self->_lf__reaction_0.num_outputs);
        blink_self->_lf__reaction_0.triggered_sizes 
                = (int*)calloc(blink_self->_lf__reaction_0.num_outputs, sizeof(int));
    }
    // Initialize the output_produced array.
    // Total number of outputs produced by the reaction.
    blink_self->_lf__reaction_1.num_outputs = 0;
    // Allocate arrays for triggering downstream reactions.
    if (blink_self->_lf__reaction_1.num_outputs > 0) {
        blink_self->_lf__reaction_1.output_produced 
                = (bool**)malloc(sizeof(bool*) * blink_self->_lf__reaction_1.num_outputs);
        blink_self->_lf__reaction_1.triggers 
                = (trigger_t***)malloc(sizeof(trigger_t**) * blink_self->_lf__reaction_1.num_outputs);
        blink_self->_lf__reaction_1.triggered_sizes 
                = (int*)calloc(blink_self->_lf__reaction_1.num_outputs, sizeof(int));
    }
    // Initialize the output_produced array.
    // Total number of outputs produced by the reaction.
    blink_self->_lf__reaction_2.num_outputs = 0;
    // Allocate arrays for triggering downstream reactions.
    if (blink_self->_lf__reaction_2.num_outputs > 0) {
        blink_self->_lf__reaction_2.output_produced 
                = (bool**)malloc(sizeof(bool*) * blink_self->_lf__reaction_2.num_outputs);
        blink_self->_lf__reaction_2.triggers 
                = (trigger_t***)malloc(sizeof(trigger_t**) * blink_self->_lf__reaction_2.num_outputs);
        blink_self->_lf__reaction_2.triggered_sizes 
                = (int*)calloc(blink_self->_lf__reaction_2.num_outputs, sizeof(int));
    }
    // Initialize the output_produced array.
    // Reaction 0 of Blink does not depend on one maximal upstream reaction.
    blink_self->_lf__reaction_0.last_enabling_reaction = NULL;
    // Reaction 1 of Blink depends on one maximal upstream reaction.
    blink_self->_lf__reaction_1.last_enabling_reaction = &(blink_self->_lf__reaction_0);
    // Reaction 2 of Blink depends on one maximal upstream reaction.
    blink_self->_lf__reaction_2.last_enabling_reaction = &(blink_self->_lf__reaction_1);
    // doDeferredInitialize
    // Connect inputs and outputs for reactor Blink.
    // END Connect inputs and outputs for reactor Blink.
    
    blink_self->_lf__reaction_0.chain_id = 1;
    // index is the OR of level 0 and 
    // deadline 140737488355327 shifted left 16 bits.
    blink_self->_lf__reaction_0.index = 0x7fffffffffff0000LL;
    blink_self->_lf__reaction_1.chain_id = 1;
    // index is the OR of level 1 and 
    // deadline 140737488355327 shifted left 16 bits.
    blink_self->_lf__reaction_1.index = 0x7fffffffffff0001LL;
    blink_self->_lf__reaction_2.chain_id = 1;
    // index is the OR of level 2 and 
    // deadline 140737488355327 shifted left 16 bits.
    blink_self->_lf__reaction_2.index = 0x7fffffffffff0002LL;
}
void _lf_trigger_startup_reactions() {
    
    for (int i = 0; i < _lf_startup_reactions_size; i++) {
        if (_lf_startup_reactions[i] != NULL) {
            _lf_enqueue_reaction(_lf_startup_reactions[i]);
        }
    }
}
void _lf_initialize_timers() {
    for (int i = 0; i < _lf_timer_triggers_size; i++) {
        if (_lf_timer_triggers[i] != NULL) {
            _lf_initialize_timer(_lf_timer_triggers[i]);
        }
    }
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
