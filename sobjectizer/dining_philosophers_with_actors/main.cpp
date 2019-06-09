#include <so_5/all.hpp>
#include <queue>
/**
 * This program represents dining philosophers on the basis of actors.
 * The fork distribution algorithm is based on priority. Forks have two kinds of priority,
 * which change after each use by the philosopher.
 * The priority is based on the parity and non-parity of the order number of the philosopher.
 */

int philosopher_count = 5; // count of philosophers
int meal_count = 2;
// messages
struct m_acquire {
    const so_5::mbox_t who;
    int phil_index;
};

struct m_change {
    int phil_index;
    std::string state;
};
struct m_release{
    const int phil_index;
};
struct m_acquired : public so_5::signal_t {};
struct m_done : public so_5::signal_t{};

// fork
class fork: public so_5::agent_t{

public:
    // constructor
    fork(context_t ctx, int index, so_5::mbox_t observer_box):
    so_5::agent_t{std::move(ctx)},
    index{index},
    observer_box{observer_box} {}
    // defines state and handlers to the corresponding events
    void so_define_agent() override {
        // initial state of fork is free
        this >>= st_free;
        log("Free", -1);
        // handle events on free state
        st_free.event([this] (mhood_t<m_acquire> msg){

            // Priority check
            if(neighbour_done) { // if one of the neighbours is no more active, then handle without considering the priority
                this>>= st_acquired;
                log("Acquired", msg->phil_index);
                so_5::send<m_acquired>(msg->who);
            }else if(index == 0) {  // if first fork then special handling
                this>>= st_acquired;
                log("Acquired", msg->phil_index);
                so_5::send<m_acquired>(msg->who);
            } else if(isEvenPhilPrioritized() && isEven(msg->phil_index)) {
                this>>= st_acquired;
                log("Acquired", msg->phil_index);
                so_5::send<m_acquired>(msg->who);
            }
            else if (isOddPhilPrioritized() && isOdd(msg->phil_index)) {
                this>>= st_acquired;
                log("Acquired", msg->phil_index);
                so_5::send<m_acquired>(msg->who);
            } else { // Adding to the queue since now the philosopher is not in priority
                log("Current Philosopher is not prioritized. Adding of the current Philosopher to the queue", msg->phil_index);
                so_5::send<m_change>(observer_box, msg->phil_index, "Q");
                waiters_queue.push(msg->who);
            }
        });

        st_acquired.event([this] (mhood_t<m_acquire> msg){
            log("Current Philosopher is not free. Adding of the current Philosopher to the queue",
                    msg->phil_index);
            so_5::send<m_change>(observer_box, msg->phil_index, "Q");
            waiters_queue.push(msg->who);
        })
        .event([this] (mhood_t<m_release> msg){
            if(waiters_queue.empty()) {
                prio_even_philosopher = !prio_even_philosopher;
                this>>=st_free;
                log("Released", msg->phil_index);
            } else {
                const auto who = waiters_queue.front();
                waiters_queue.pop();
                log("Released and Acquired by waiting Philosopher", msg->phil_index);
                so_5::send<m_acquired>(who);
            }

        }).event([this](mhood_t<m_done> msg){
                    neighbour_done = true;

        });

    }


    bool isEvenPhilPrioritized(){
        return  prio_even_philosopher;
    }

    bool isOddPhilPrioritized(){
        return  !prio_even_philosopher;
    }

    bool isOdd (int index) {
        return !isEven(index);
    }

    bool isEven (int index) {
        return index % 2 == 0;
    }

    void log(std::string message, int phil_index) {
        std::cout<<"Fork ["<<index<<"] is interacting with Philosopher [" <<phil_index<<"] -> "<<message<<std::endl;
    }

    void log(std::string message) {
        std::cout<<"Fork["<<index<<"]-> "<<message<<std::endl;
    }

private:
    const state_t st_free {this, "Free"};
    const state_t st_acquired {this, "Acquired"};
    bool prio_even_philosopher = false;
    const int index;
    bool neighbour_done = false;
    so_5::mbox_t observer_box;
    std::queue<so_5::mbox_t> waiters_queue;
};


// philosopher
class philosopher: public so_5::agent_t {
public:

   philosopher (context_t ctx,
           so_5::mbox_t left_fork_box,
           so_5::mbox_t right_fork_box,
           so_5::mbox_t observer_box,
           int index, int meal_count)
            : so_5::agent_t{std::move(ctx)},
            left_fork_box{std::move(left_fork_box)},
            right_fork_box{std::move(right_fork_box)},
            observer_box{observer_box},
            index{index},
            meal_count{meal_count} {}

    void so_define_agent () override {
        // in thinking state we must handle m_stop_thinking message
        // in order to turn the state to Left Waiting state
        st_thinking.event([=](mhood_t<m_stop_thinking>){
            this>>=st_left_waiting;
            so_5::send<m_change>(observer_box, index, "L");
            log("Left Waiting");
            so_5::send<m_acquire>(left_fork_box, so_direct_mbox(), index);
        });

        // if philosopher acquires the left fork then change state to Right waiting and
        // send Acquire message to the right fork
        st_left_waiting.event([=] (mhood_t<m_acquired> msg) {
            log("Right waiting");
            this>>=st_right_waiting;
            so_5::send<m_change>(observer_box, index, "R");
            so_5::send<m_acquire>(right_fork_box, so_direct_mbox(), index);
        });

        // if philosopher has acquired two forks the change the state to Eating.
        // Then require stopping of the Eating state
        st_right_waiting.event([=](mhood_t<m_acquired> msg) {
            this>>=st_eating;
            so_5::send<m_change>(observer_box, index, "E");
            log("Eating");
            so_5::send<m_stop_eating>(*this);
        });

        // In Eating state we hanlde stop eating message
        // If there is meal then change the state to Thinking
        // If there is no meal the change the state to Done
        st_eating.event([=] (mhood_t<m_stop_eating> msg) {
            meal_count--;
            if(meal_count == 0) {
                this>>=st_done;
            }else {
                this>>= st_thinking;
                so_5::send<m_change>(observer_box, index, "T");
                log("Thinking");

                so_5::send<m_release>(left_fork_box, index);
                so_5::send<m_release>(right_fork_box, index);
                so_5::send<m_stop_thinking>(*this);
            }
        });
        // On the enter to the Done state we must release all acquired forks.
        // Also the neighbour forks must be informed that the current philosopher
        // will not more try  to acquire the neighbour forks in order to optimize the
        // fork sharing process
        st_done.on_enter([=]{
            std::cout<<"Philosopher with index "<<index<<" has finished his work"<<std::endl;
            log("Done");
            so_5::send<m_done>(left_fork_box);
            so_5::send<m_done>(right_fork_box);
            so_5::send<m_release>(left_fork_box, index);
            so_5::send<m_release>(right_fork_box, index);
            so_5::send<m_done>(observer_box);
        });
   }

   void so_evt_start() override {
       this>>= st_thinking;
       so_5::send<m_change>(observer_box, index, "T");
       log("Thinking");
       so_5::send<m_stop_thinking>(*this);
   }

   void log(std::string message) {
       std::cout<<"Phil ["<<index<<"] -> "<<message<<std::endl;
   }

private:
    state_t st_thinking {this, "Thinking"};
    state_t st_left_waiting {this, "Waiting for left Fork"};
    state_t st_right_waiting {this, "Waiting for right Fork"};
    state_t st_eating {this, "Eating"};
    state_t st_done {this, "Done"};
    so_5::mbox_t left_fork_box;
    so_5::mbox_t right_fork_box;
    so_5::mbox_t observer_box;
    const int index;
    int meal_count;
    struct m_stop_thinking: public so_5::signal_t{};
    struct m_stop_eating: public so_5::signal_t{};
};

/**
 * actor that observes the state of the philoshophers.
 * At the end of the dining philosophers it prints all the states of each
 * philosopher
 */
class state_observer: public so_5::agent_t {
public:

    state_observer(context_t ctx, int phil_count):  so_5::agent_t{std::move(ctx)}, phil_count{phil_count} {}

    void so_define_agent() override {
        this>>=st_observing;

        st_observing
                .event([this](mhood_t<m_change> msg){
                    states [msg->phil_index] = states [msg->phil_index] + msg->state;
                })
                .event([this] (mhood_t<m_done>){
                    done_phil_count ++;
                    if(done_phil_count == phil_count) {
                        std::cout<<"-----------------------------------"<<std::endl;
                        std::cout<<"T - Thinking; Q - Queued; R - Right Waiting: L - Left Waiting; E - Eating."<<std::endl;
                        std::cout<<"Result:"<<std::endl;
                        for (int i = 0; i < phil_count; i++) {
                            std::cout<<"Philosopher["<<i<<"] -> "<<states[i]<<std::endl;
                        }
                    }
                });
    }
private:
    const state_t st_observing{this, "Observing"};
    const int phil_count;
    int done_phil_count = 0;
    std::map<int, std::string> states;
};

// define factory function, that creates actors
void simulate_dining_philosophers ( int phil_count, int meal_count) {
    so_5::launch( [&]( so_5::environment_t & env ) {
        env.introduce_coop( [&]( so_5::coop_t & coop ) {
            std::vector<so_5::agent_t * > forks(phil_count, nullptr);
            // create state_observer
            so_5::agent_t * observer  = coop.make_agent<state_observer>(phil_count);
            // create forks
            for (int i = 0; i < phil_count; i++) {
                forks[i] = coop.make_agent<fork>(i, observer->so_direct_mbox());
            }
            //create philosophers
            for (int i = 0; i < phil_count-1; i++) {
                coop.make_agent<philosopher>(
                        forks[i+1]->so_direct_mbox(),
                        forks[i]->so_direct_mbox(),
                        observer->so_direct_mbox(),
                        i,
                        meal_count
                        );
            }
            // special case for the last philosopher
            coop.make_agent<philosopher>(
                    forks[0]->so_direct_mbox(),
                    forks[phil_count-1]->so_direct_mbox(),
                    observer->so_direct_mbox(),
                    phil_count-1,
                    20 // meal count 20 to demonstrate, that the program can work with actors have different meal count
            );
        }

    );
    });

}

int main(){
    simulate_dining_philosophers(philosopher_count, meal_count);
    return 0;
}