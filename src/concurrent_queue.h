#include <memory>
#include <mutex>

template<typename T> class queue 
{ 
private:  
    struct node
    { 
        std::unique_ptr<T> data; 
        std::unique_ptr<node> next;  
    };

    std::unique_ptr<node> head;
    std::mutex head_mutex;
    node* tail;
    std::mutex tail_mutex;

    node* get_tail()
    {
        std::lock_guard<std::mutex> g(tail_mutex);
        return tail;
    }

    std::unique_ptr<node> pop_head()
    {
        std::lock_guard<std::mutex> g(head_mutex);
        if(head.get() == get_tail())
        {
            return nullptr;
        }
        std::unique_ptr<node> old_head = std::move(head);
        head = std::move(old_head->next);
        return old_head;
    }

public: 

    queue(): head(new node), tail(head.get()) {}
    
    queue(const queue& other)=delete;
    
    queue& operator=(const queue& other)=delete;
    
    std::unique_ptr<T> try_pop()
    {
        std::unique_ptr<node> old_head = pop_head();
        return old_head ? std::move(old_head->data) : std::unique_ptr<T>();
    }

    void push(T new_value)
    {
        std::unique_ptr<T> new_data(
                std::make_unique<T>(std::move(new_value)));
        std::unique_ptr<node> p(new node);
        node* const new_tail = p.get();
        std::lock_guard<std::mutex> g(tail_mutex);
        tail->data = std::move(new_data);
        tail->next = std::move(p);
        tail = new_tail;
    } 
};

