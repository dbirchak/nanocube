#include <simple_nanocube.hh>

#include <iostream>
#include <functional>
#include <algorithm>
#include <iterator>
#include <sstream>
#include <cassert>

#include <log.hh>


#define LOG

//-----------------------------------------------------------------------------
// Content
//-----------------------------------------------------------------------------

void Content::setOwner(Node *owner) {
    this->owner = owner;
}

//-----------------------------------------------------------------------------
// Summary
//-----------------------------------------------------------------------------

void Summary::insert(const Object &obj) {
    std::cout << "insert " << obj << std::endl;
    objects.insert(obj);
    // ++count;
}

std::string Summary::info() const {
    std::vector<Object> objects_list(objects.begin(), objects.end());
    std::sort(objects_list.begin(), objects_list.end());

    std::stringstream ss;
    std::copy(objects_list.begin(), objects_list.end(), std::ostream_iterator<Object>(ss, " "));

    return ss.str();
}


//-----------------------------------------------------------------------------
// ContentLink
//-----------------------------------------------------------------------------

ContentLink::ContentLink(Content *content, LinkType link_type):
    content(content), link_type(link_type)
{}

//-----------------------------------------------------------------------------
// ParentChildLink
//-----------------------------------------------------------------------------

ParentChildLink::ParentChildLink(Label label, Node *child, LinkType link_type):
    label(label), child(child), link_type(link_type)
{}

//-----------------------------------------------------------------------------
// Nanocube
//-----------------------------------------------------------------------------

Nanocube::Nanocube(const std::vector<int> &levels):
    levels { levels },
    dimension { (int) levels.size() }
{}

Summary *Nanocube::query(const Address &addr)
{
    if (root == nullptr) {
        return nullptr;
    }

    //
    Node *node = root;
    for (size_t i=0;i<addr.size();i++) {
        for (size_t j=0;j<addr[i].size();j++) {
            node = node->getChild(addr[i][j]);
            if (node == nullptr)
                return nullptr;
        }
        if (i < addr.size()-1) {
            node = node->getContentAsNode();
        }
    }

    if (node) {
        return node->getContentAsSummary();
    }
    else {
        return nullptr;
    }
}

namespace highlight {
    static const int CLEAR          = 0;
    static const int MAIN           = 1;
    static const int PARALLEL       = 2;
    static const int UPSTREAM_CHECK = 3;
}


struct LogMsg {
    LogMsg(std::string msg) {
        logging::getLog().pushMessage(msg);
    }
    ~LogMsg() {
        logging::getLog().popMessage();
    }
};

std::string log_name(Content* content) {
    return logging::name((logging::NodeID) content);
}

//std::string log_name(Summary* summary) {
//    return logging::name((logging::NodeID) summary);
//}

static void log_new_node(void* node, int dim, int layer) {
    logging::getLog().newNode((logging::NodeID)node, dim, layer);
}

static void log_shallow_copy(Node* node, int dim, int layer) {

    logging::getLog().newNode((logging::NodeID)node, dim, layer);
    if (node->getContent() != nullptr) {
        logging::getLog().setContentLink((logging::NodeID) node, (logging::NodeID) node->getContent(), logging::SHARED);
    }
    for (auto &child_link: node->children) {
        logging::getLog().setChildLink((logging::NodeID) node, (logging::NodeID) child_link.child, child_link.label, logging::SHARED);
    }
}

static void log_update_child_link(Node* node, Label label) {
    LinkType link_type;
    Node* child = node->getChild(label,link_type);
    logging::getLog().setChildLink((logging::NodeID) node,
                                   (logging::NodeID) child,
                                   label,
                                   link_type == SHARED ? logging::SHARED : logging::PROPER);
}



static void log_shallow_copy(Summary* summary, int dim, int layer) {

    logging::getLog().newNode((logging::NodeID)summary, dim, layer);
    std::vector<logging::Object> objects(summary->objects.size());
    std::copy(summary->objects.begin(),summary->objects.end(),objects.begin());
    std::sort(objects.begin(),objects.end());
    for (auto obj: objects) {
        logging::getLog().store((logging::NodeID)summary, obj);
    }
}

static void log_update_content_link(Node* node) {
    logging::getLog().setContentLink((logging::NodeID)node,
                                     (logging::NodeID)node->getContent(),
                                     node->getContentType() == SHARED ? logging::SHARED : logging::PROPER);
}

//static void log_update_content_link(Node* node) {
//    LinkType link_type = node->getContentType();
//    void* content = node->getContent();
//    logging::getLog().setContentLink((logging::NodeID) node,
//                                       (logging::NodeID) content,
//                                       link_type == SHARED ? logging::SHARED : logging::PROPER);
//}

static void log_highlight_node(const Node* node, logging::Color color) {
    logging::getLog().highlightNode((logging::NodeID)node, color);
}

static void log_highlight_child_link(const Node* node, Label label, logging::Color color) {
    logging::getLog().highlightChildLink((logging::NodeID)node,label,color);
}

static void log_highlight_content_link(const Node* node, logging::Color color) {
    logging::getLog().highlightContentLink((logging::NodeID)node,color);
}





//-----------------------------------------------------------------------------
// UpstreamThread
//-----------------------------------------------------------------------------

struct UpstreamThread {
public:
    UpstreamThread(Node *node);
    virtual ~UpstreamThread();
    Node* getNext() const;
    Node *getNext(bool &is_content) const;
    void  advance();
    void  rewind();
    void  pop();
    Node* top();
public:
    std::vector<Node*> stack;
};

//-----------------------------------------------------------------------------
// UpstreamThread Impl.
//-----------------------------------------------------------------------------

UpstreamThread::UpstreamThread(Node *node)
{
    stack.push_back(node);
#ifdef LOG
    log_highlight_node(node, highlight::UPSTREAM_CHECK);
#endif
}

UpstreamThread::~UpstreamThread() {
    while (!stack.empty()) {
        rewind();
    }
}

Node* UpstreamThread::getNext(bool &is_content) const {
    Node* top = stack.back();
    if (top->proper_parent != nullptr) {
        is_content = false;
        return top->proper_parent;
    }
    else {
        is_content = true;
        return top->owner;
    }
}

Node* UpstreamThread::getNext() const {
    bool is_content;
    return getNext(is_content);
}

void UpstreamThread::advance() {
    bool is_content;
    stack.push_back(this->getNext(is_content));

#ifdef LOG
    Node* child  = *(stack.rbegin()+1);
    Node* parent = stack.back();
    if (is_content) {
        log_highlight_content_link(parent, highlight::UPSTREAM_CHECK);
    }
    else {
        log_highlight_child_link(parent, child->label, highlight::UPSTREAM_CHECK);
    }
    log_highlight_node(parent, highlight::UPSTREAM_CHECK);
#endif
}

void UpstreamThread::rewind() {
#ifdef LOG
    Node* parent = stack.back();
    log_highlight_node(parent, highlight::CLEAR);
    if (stack.size() > 1) {
        Node* child  = *(stack.rbegin()+1);
        if (child->proper_parent != nullptr) {
            log_highlight_child_link(parent, child->label, highlight::CLEAR);
        }
        else {
            log_highlight_content_link(parent, highlight::CLEAR);
        }
    }
#endif
    stack.pop_back();
}

//-----------------------------------------------------------------------------
// Thread
//-----------------------------------------------------------------------------

struct Thread {
public:
    enum ThreadType { UNDEFINED, MAIN, PARALLEL };
    enum ItemType   { ROOT, CHILD, CONTENT };
public:
    struct Item {
        Item() = default;
        Item(Node* node, ItemType item_type, int dim, int layer);
        Node*    node      { nullptr };
        ItemType item_type { ROOT };
        int      dim       { 0 };
        int      layer     { 0 };
    };
private:
    void flagTopNode(bool b);
public:
    Thread() = default;
    Thread(ThreadType thread_type);

    void  start(Node *node, int dim, int layer);
    void  advanceContent();
    void  advanceChild(Label label);
    void  rewind();
    int   getCurrentDimension() const;
    int   getCurrentLayer() const;
    Node* top() const;
public:
    ThreadType        thread_type;
    std::vector<Item> stack;
};

//-----------------------------------------------------------------------------
// Thread::Item Impl.
//-----------------------------------------------------------------------------

Thread::Item::Item(Node *node, ItemType item_type, int dim, int layer):
    node(node),
    item_type(item_type),
    dim(dim),
    layer(layer)
{}

//-----------------------------------------------------------------------------
// Thread Impl.
//-----------------------------------------------------------------------------

Thread::Thread(Thread::ThreadType thread_type):
    thread_type(thread_type)
{}

void Thread::flagTopNode(bool b) {
    if (b) {
        if (thread_type == MAIN) {
            top()->setFlag(Node::IN_MAIN_PATH);
        }
        else if (thread_type == PARALLEL) {
            top()->setFlag(Node::IN_PARALLEL_PATH);
        }
    }
    else {
        top()->setFlag(Node::NONE);
    }
}

int Thread::getCurrentDimension() const {
    return stack.back().dim;
}

int Thread::getCurrentLayer() const {
    return stack.back().layer;
}

void Thread::start(Node* node, int dim, int layer) {
    assert(stack.empty());
    stack.push_back(Item(node,ROOT,dim,layer));
    flagTopNode(true);

#ifdef LOG
    log_highlight_node(node, thread_type == MAIN ? highlight::MAIN : highlight::PARALLEL);
#endif

}

void Thread::advanceContent() {
    assert(!stack.empty());

    // assuming content is a node
    Node* content = this->top()->getContentAsNode();

    assert (content);

#ifdef LOG
    log_highlight_content_link(this->top(), thread_type == MAIN ? highlight::MAIN : highlight::PARALLEL);
    log_highlight_node(content, thread_type == MAIN ? highlight::MAIN : highlight::PARALLEL);
#endif

    stack.push_back(Item(content, CONTENT, getCurrentDimension()+1, 0));
    flagTopNode(true);


}

void Thread::advanceChild(Label label) {
    assert(!stack.empty());

    // assuming content is a node
    Node* child = this->top()->getChild(label);
    assert (child);

#ifdef LOG
    log_highlight_child_link(this->top(), label, thread_type == MAIN ? highlight::MAIN : highlight::PARALLEL);
    log_highlight_node(child, thread_type == MAIN ? highlight::MAIN : highlight::PARALLEL);
#endif

    stack.push_back(Item(child, CHILD, getCurrentDimension(), getCurrentLayer()+1));
    flagTopNode(true);
}

Node* Thread::top() const {
    return stack.back().node;
}

void Thread::rewind() {
    flagTopNode(false); // unflag
    Item item = stack.back();
    stack.pop_back();

#ifdef LOG
    log_highlight_node(item.node, highlight::CLEAR);
    if (item.item_type == CONTENT) {
        log_highlight_content_link(top(), highlight::CLEAR);
    }
    else if (item.item_type == CHILD) {
        log_highlight_child_link(top(), item.node->label, highlight::CLEAR);
    }
#endif
}

//-----------------------------------------------------------------------------
// ThreadStack
//-----------------------------------------------------------------------------

struct ThreadStack {
public:
    ThreadStack(Thread::ThreadType thread_type);
    void pop();
    void rewind();
    void push(Node* root, int dimension, int layer);
    void advanceContent();
    void advanceChild(Label label);
    Node* getFirstProperChild(Label label) const;
    Content* getAnyContent() const;
    Summary* getFirstSummary() const;
    Thread& top();
public:
    Thread::ThreadType thread_type;
    std::vector<Thread> stack;
};

//-----------------------------------------------------------------------------
// ThreadStack Impl.
//-----------------------------------------------------------------------------

ThreadStack::ThreadStack(Thread::ThreadType thread_type):
    thread_type(thread_type)
{}

void ThreadStack::pop() {
    stack.pop_back();
}

void ThreadStack::push(Node* root, int dimension, int layer)
{
    stack.push_back(Thread(thread_type));
    stack.back().start(root, dimension, layer);
}

void ThreadStack::advanceChild(Label label)
{
    for (auto &th: stack) {
        th.advanceChild(label);
    }
}

void ThreadStack::rewind()
{
    for (auto &th: stack) {
        th.rewind();
    }
}

Node *ThreadStack::getFirstProperChild(Label label) const
{
    LinkType link_type;
    for (auto &th: stack) {
        Node* child = th.top()->getChild(label, link_type);
        if (link_type == PROPER) {
            return child;
        }
    }
    return nullptr;
}

Content* ThreadStack::getAnyContent() const
{
    for (const Thread &th: stack) {
        Content* content = th.top()->getContent();
        return content;
    }
    return nullptr;
}

Summary* ThreadStack::getFirstSummary() const
{
    for (const Thread &th: stack) {
        return th.top()->getContentAsSummary();
    }
    return nullptr;
}

Thread &ThreadStack::top()
{
    return stack.back();
}

void ThreadStack::advanceContent()
{
    for (auto &th: stack) {
        th.advanceContent();
    }
}

//-----------------------------------------------------------------------------
// Nanocube Insert
//-----------------------------------------------------------------------------

void Nanocube::insert(const Address &addr, const Object &object)
{

#ifdef LOG
    std::stringstream ss;
    ss << "Insert obj: " << object << " addr: ";
    for (size_t i=0;i<addr.size();++i) {
        ss << "{";
        std::ostream_iterator<int> out_it (ss,",");
        std::copy ( addr[i].begin(), addr[i].end(), out_it );
        ss << "} ";
    }
    LogMsg log_msg(ss.str());
#endif



    Thread       main_thread(Thread::MAIN);
    ThreadStack  parallel_threads(Thread::PARALLEL);


    if (root == nullptr) {
        root = new Node();
#ifdef LOG
        logging::getLog().newNode((logging::NodeID)root, 0, 0);
#endif
    }

    std::function<void(Thread&, ThreadStack&)> insert_recursively;

    int dimension = this->dimension;


    insert_recursively = [&insert_recursively, &addr, &object, &dimension]
            (Thread& main_thread, ThreadStack& parallel_threads) -> void {

#ifdef LOG
        std::stringstream ss;
        auto &log = logging::getLog();
        ss << "insert_recursively on node " << log.getNodeName((logging::NodeID) main_thread.top()) << " dim " << main_thread.getCurrentDimension();
        LogMsg log_insert_recursively(ss.str());
#endif

        //
        // Part 1: Advance main and parallel threads until:
        //
        // (a) the top of the main thread has a shared child that
        //     can be proven to be contained in one of the parallel
        //     threads. In this case switch the shared child accordingly
        //     and stop.
        //
        // (b) top of the main thread has no child, in which case
        //     we can share from a proper parallel child or in case
        //     it doesn't exist, create a new child on the main branch
        //

        auto &dim_addr = addr[main_thread.getCurrentDimension()];

        int index = 0;
        while (index < (int) dim_addr.size()) {

            Label label  = dim_addr[index];

            Node* parent = main_thread.top();

            LinkType link_type;
            Node* child = parent->getChild(label, link_type);

            if (child) {
                if (link_type == SHARED) {
                    // check if we can end Part 1 by preserving
                    // or reswitching child node.

                    bool can_switch = false;
                    {
                        UpstreamThread upstream_thread(child);
                        while (true) {
                            Node *aux = upstream_thread.getNext();
                            if (aux->flag == Node::IN_PARALLEL_PATH) {
                                can_switch = true;
                                break;
                            }
                            else if (aux->flag == Node::IN_MAIN_PATH) {
                                break;
                            }
                            upstream_thread.advance();
                        }
                        // TODO: on the destructor of upstream thread pop everything
                        // and generate actions accordingly
                    }

                    //
                    if (can_switch) {
                        Node* new_child = parallel_threads.getFirstProperChild(label);
                        if (!new_child) {
                            throw std::runtime_error("oops");
                        }
                        if (new_child != child) {
                            parent->setParentChildLink(label, new_child, SHARED);
                            break;
#ifdef LOG
                            log_update_child_link(parent,label);
#endif
                        }
                    }
                    else {
                        Node *new_child = child->shallowCopy();
#ifdef LOG
                        {
                            LogMsg msg("...cannot switch; shallow copy of " + log_name(child) +" into " + log_name(new_child));
                            log_shallow_copy(new_child,main_thread.getCurrentDimension(),main_thread.getCurrentLayer()+1);
                        }
#endif
                        parent->setParentChildLink(label, new_child, PROPER);
#ifdef LOG
                        {
                            LogMsg msg("...setting shallow copied node as child " + std::to_string(label) + " of node " + log_name(parent));
                            log_update_child_link(parent,label);
                        }
#endif
                    }
                }
            } // is there a child?
            else {
                // find a parallel proper child share and end Part 1
                Node* new_child = parallel_threads.getFirstProperChild(label);
                if (new_child) {
                    parent->setParentChildLink(label, new_child, SHARED);
#ifdef LOG
                    log_update_child_link(parent,label);
#endif
                    break;
                }
                else {
                    parent->setParentChildLink(label, new Node(), PROPER);
#ifdef LOG
                    log_new_node(parent->getChild(label),main_thread.getCurrentDimension(),main_thread.getCurrentLayer()+1);
                    log_update_child_link(parent,label);
#endif
                }
            }

            main_thread.advanceChild(label);
            parallel_threads.advanceChild(label);

            ++index;
        }


        // Last node dimension
        bool last_node_dim = (dimension-1 == main_thread.getCurrentDimension());

        // Part 2: check the content of the main thread
        for (;index >= 0;--index) {

            std::stringstream ss;
            ss << "index" << index
               << "  dim: " << main_thread.getCurrentDimension()
               << "  layer: " << main_thread.getCurrentLayer()
               << "  last_node_dim: " << last_node_dim;

            std::cout << ss.str() << std::endl;

            LogMsg msg(ss.str());

            Node* parent = main_thread.top();

            // general case
            if (parent->getNumChildren() == 1) {
                Label label = dim_addr[index];
                Node* child = parent->getChild(label);
                parent->setContent(child->getContent(), SHARED); // sharing content with children
#ifdef LOG
                log_update_content_link(parent);
#endif

                // done updating
            }

            else if (!last_node_dim) {
                // Intermediate dimension

                if (parent->hasContent() == false) {
                    // why not parallel contents?
                    parent->setContent(new Node(), PROPER); // create proper content

#ifdef LOG
                    log_new_node(parent->getContentAsNode(),main_thread.getCurrentDimension()+1,0);
                    log_update_content_link(parent);
#endif
                }
                else if (parent->getContentType() == SHARED) {
                    // why not parallel contents?
                    Node* old_content = parent->getContentAsNode();
                    parent->setContent(old_content->shallowCopy(), PROPER); // create proper content

#ifdef LOG
                    log_shallow_copy(parent->getContentAsNode(),main_thread.getCurrentDimension()+1,0);
                    log_update_content_link(parent);
#endif
                }

                // there is a parallel content
                if (parent->getNumChildren() > 0) {
                    Label label = dim_addr[index];
                    Node* child = parent->getChild(label);
                    parallel_threads.push(child, main_thread.getCurrentDimension(), main_thread.getCurrentLayer()+1);
                }

                // go to next dimension
                main_thread.advanceContent();
                parallel_threads.advanceContent();

                insert_recursively(main_thread, parallel_threads);

                //parallel_threads.rewind();
                //main_thread.rewind();

                if (parent->getNumChildren() > 0) {
                    parallel_threads.top().rewind();
                    parallel_threads.pop();
                }

            }
            else {
                // Last node dimension. Next dimension contain summaries.
                if (parent->hasContent() == false)
                {
                    Summary* parallel_summary = parallel_threads.getFirstSummary();
                    if (parallel_summary) {
                        parent->setContent(parallel_summary, SHARED); // create proper content
#ifdef LOG
                        log_update_content_link(parent);
#endif
                    }
                    else {
                        parent->setContent(new Summary(), PROPER); // create proper content
                        parent->getContentAsSummary()->insert(object);
#ifdef LOG
                        log_new_node(parent->getContent(),main_thread.getCurrentDimension()+1,0);
                        log_update_content_link(parent);
                        logging::getLog().store((logging::NodeID)parent->getContent(), object);
#endif
                    }
                }
                else {
                    if (parent->getContentType() == SHARED) {
                        // check if we can switch or we have to shallow copy
                        // and insert into a new proper content
                        bool can_switch = false;

                        // test first case
                        Node* aux = parent->getContent()->owner;
                        if (aux->flag == Node::IN_PARALLEL_PATH) {
                            can_switch = true;
                        }
                        else if (aux->flag == Node::IN_MAIN_PATH) {
                            can_switch = false;
                        }
                        else {
                            UpstreamThread upstream_thread(aux);
                            while (true) {
                                aux = upstream_thread.getNext();
                                if (aux->flag == Node::IN_PARALLEL_PATH) {
                                    can_switch = true;
                                    break;
                                }
                                else if (aux->flag == Node::IN_MAIN_PATH) {
                                    break;
                                }
                                upstream_thread.advance();
                            }
                        }
                        if (can_switch) {
                            Summary* parallel_summary = parallel_threads.getFirstSummary();
                            parent->setContent(parallel_summary, PROPER); // create proper content
#ifdef LOG
                            log_update_content_link(parent);
#endif
                        }
                        else {
                            // shallow copy old summary
                            Summary* old_summary = parent->getContentAsSummary();
                            parent->setContent(new Summary(*old_summary), PROPER); // create proper content
#ifdef LOG
                            {
                                LogMsg msg("...shallow copy of " +  log_name(old_summary)
                                           + " into " + log_name(parent->getContentAsSummary()));
                                log_shallow_copy(parent->getContentAsSummary(),main_thread.getCurrentDimension()+1,0);
                            }
                            {
                                LogMsg msg("...setting content link of node " +  log_name(parent));
                                log_update_content_link(parent);
                            }
#endif
                            parent->getContentAsSummary()->insert(object);
#ifdef LOG
                            {
                                LogMsg msg("...storing object into " +  log_name(parent->getContent()));
                                logging::getLog().store((logging::NodeID)parent->getContent(), object);
                            }
#endif
                        }
                    }
                    else {
                        parent->getContentAsSummary()->insert(object);
#ifdef LOG
                        logging::getLog().store((logging::NodeID)parent->getContent(), object);
#endif
                    }
                }
            }

            parallel_threads.rewind();
            main_thread.rewind();
        }

//        main_thread.rewind();
//        parallel_threads.rewind();

    }; // end lambda function insert_recursively

    // start main thread
    main_thread.start(root,0,0);

    // insert
    insert_recursively(main_thread, parallel_threads);

}

#if 0








Node* current_root, int current_dim, const Node* child_updated_root



std::cout << "insert_recursively, dim:" << current_dim << std::endl;


auto &dim_path = paths[current_dim];
auto &dim_parallel_path = parallel_paths[current_dim];
auto &dim_addr = addr[current_dim];

dim_path.clear();
dim_parallel_path.clear();

// fill in dim_stacks with an outdated path plus (a null pointer or shared child)
prepareOutdatedProperPath(current_root, child_updated_root, dim_addr, dim_path, dim_parallel_path, current_dim);

Node *child = dim_path.back(); // by convention the top of the stack
                               // is a node that is already updated or
                               // a nullptr

auto index = dim_path.size()-2;
for (auto it=dim_path.rbegin()+1;it!=dim_path.rend();++it) {

    Node* parent = *it;
    const Node* parallel_parent = (index < dim_parallel_path.size() ? dim_parallel_path[index] : nullptr);

    {
        LogMsg msg("...parallel_path index: " + std::to_string(index) + "  size: " + std::to_string(dim_parallel_path.size()));
    }

    --index;


    if (parent->children.size() == 1) {
        parent->setContent(child->getContent(), SHARED); // sharing content with children

#ifdef LOG
        {
            LogMsg msg_single_child("...detected single child, sharing its contents");
            log_update_content_link(parent);
        }
#endif

    }
    else {
        if (current_dim == dimension-1) {
            // last dimension: insert object into summary
            if (parent->hasContent() == false) {
                parent->setContent(new Summary(), PROPER); // create proper content

#ifdef LOG
                {
                    LogMsg msg_single_child("...new summary");
                    log_new_node(parent->getContent(), current_dim+1, 0);
                    // logging::getLog().newNode((logging::NodeID) parent->getContent(), current_dim+1, 0);
                    log_update_content_link(parent);
                }
#endif

            }
            else if (parent->getContentType() == SHARED) {
                // since number of children is not one, we need to clone
                // content and insert new object
                Summary* old_content = parent->getContentAsSummary();
                parent->setContent(new Summary(*parent->getContentAsSummary()), PROPER); // create proper content

#ifdef LOG
                {
                    LogMsg msg("...shallow copy of summary: " + std::to_string(logging::getLog().getNodeName((logging::NodeID)old_content)));
                    log_shallow_copy(parent->getContentAsSummary(), current_dim+1, 0);
                    log_update_content_link(parent);
                }
#endif


            }

#ifdef LOG
            {
                LogMsg msg_single_child("...inserting " + std::to_string(object) + " into summary node");
                logging::getLog().store((logging::NodeID)parent->getContent(), object);
            }
#endif

            parent->getContentAsSummary()->insert(object);
        }
        else {
            // not last dimension
            if (parent->hasContent() == false) {
                parent->setContent(new Node(), PROPER); // create proper content

#ifdef LOG
                {
                    LogMsg msg_single_child("...new content on dimension " + std::to_string(current_dim+1));
                    log_new_node(parent->getContent(), current_dim+1, 0);
                    // logging::getLog().newNode((logging::NodeID)parent->getContent(), current_dim+1, 0);
                    log_update_content_link(parent);
                }
#endif



            }
            else if (parent->getContentType() == SHARED) {
                // need to copy
                Node* old_content = parent->getContentAsNode();
                parent->setContent(parent->getContentAsNode()->shallowCopy(), PROPER); // create proper content

#ifdef LOG
                {
                    LogMsg msg("...shared branch needs to be copied. Shallow copy of node: " + std::to_string(logging::getLog().getNodeName((logging::NodeID)old_content)));
                    log_shallow_copy(parent->getContentAsNode(), current_dim+1, 0);
                    log_update_content_link(parent);
                }
#endif
            }

//                    Node* parallel_content = parallel_contents[current_dim]; //nullptr;
//                    if (!parallel_content && child) {
//                        parallel_content = child->getContentAsNode();
//                    }

            // here is the catch
            const Node* parallel_content = nullptr;
            if (parallel_parent) {
                parallel_content = parallel_parent->getContentAsNode();
            }
            else if (child) {
                parallel_content = child->getContentAsNode();
            }

//                    else {
//                        parallel_content = parallel_contents[current_dim];
//                    }


#ifdef LOG
            {
                log_highlight_content_link(parent, highlight::MAIN);
                if (parallel_parent) {
                    log_highlight_content_link(parallel_parent, highlight::PARALLEL);
                }
            }
#endif

            insert_recursively(parent->getContentAsNode(), current_dim+1, parallel_content);

#ifdef LOG
            {
                log_highlight_content_link(parent, highlight::CLEAR);
                if (parallel_parent) {
                    log_highlight_content_link(parallel_parent, highlight::CLEAR);
                }
            }
#endif


//                     update parallel content (for coarser contents on that dimension)
            // parallel_contents[current_dim] = parent->getContentAsNode();
            //}
        }
    }

#ifdef LOG
    {
        std::string st_msg = parallel_parent? "...moving up parent and parallel_parent" : "...moving up parent only";
        LogMsg msg(st_msg);
        log_highlight_node(parent, highlight::CLEAR);
        if (parent->proper_parent != nullptr) {
            log_highlight_child_link(parent->proper_parent,
                                     parent->label,
                                     highlight::CLEAR);
        }
#endif
        // clear flags on parent and parallel_parent
        parent->setFlag(Node::NONE);
        if (parallel_parent) {
            parallel_parent->setFlag(Node::NONE);
        }

#ifdef LOG
        parent->setFlag(Node::NONE);
        if (parallel_parent) {
            parallel_parent->setFlag(Node::NONE);
            log_highlight_node(parallel_parent, highlight::CLEAR);
            if (parallel_parent->proper_parent != nullptr) {
                log_highlight_child_link(parallel_parent->proper_parent,
                                         parallel_parent->label,
                                         highlight::CLEAR);
            }
        }
    }
#endif

    child = parent;
}

};






//-----------------------------------------------------------------------------
// prepareOutdatedProperPath
//-----------------------------------------------------------------------------

// void prepareOutdatedProperPath(const Node* child_updated_path, const DimAddress &addr, std::vector<Node*> &result_path, int current_dim);
void prepareOutdatedProperPath(Node*                     root,
                               const Node*               parallel_root,
                               const DimAddress          &addr,
                               std::vector<Node *>       &result_path,
                               std::vector<const Node *> &parallel_path,
                               int current_dim)
{
#ifdef LOG
    std::vector<const Node*> parallel_highlight_stack;
#endif


    size_t index = 0;

    auto attachNewProperChain = [&index, &addr, &result_path, &current_dim](Node* node) {
        Node* next = new Node(); // last node

#ifdef LOG
        log_new_node(next, current_dim, result_path.size());
#endif

        for (size_t i=addr.size()-1; i>index; --i) {
            Node* current = new Node();
            bool created = false;
            current->setParentChildLink(addr[i], next, PROPER, created);


#ifdef LOG
            log_new_node(current, current_dim, i);
            log_update_child_link(current, addr[i]);
#endif

            next = current;
        }

        { // join the given node with new chain
            bool created = false;
            node->setParentChildLink(addr[index], next, PROPER, created);

#ifdef LOG
            log_update_child_link(node, addr[index]);
#endif
        }

        { // update path
            Node *current = node;
            for (size_t i=index;i<addr.size();i++) {
                Node *child = current->getChild(addr[i]);
                result_path.push_back(child);
                current = child;
            }
            result_path.push_back(nullptr); // end marker
        }
    };




    // start with root node and previous node is nullptr
    Node*    current_node  = root;
    result_path.push_back(current_node);
    current_node->setFlag(Node::IN_MAIN_PATH);

    // pointer parallel to current_node in child_structure
    const Node* current_parallel_node  = parallel_root;
    if (current_parallel_node != nullptr) {
        parallel_path.push_back(current_parallel_node);
        current_parallel_node->setFlag(Node::IN_PARALLEL_PATH);
    }

//    std::cout << "current_address.level: " << current_address.level << std::endl;
//    std::cout << "address.level:         " << address.level         << std::endl;

#ifdef LOG
    {
        std::string st_msg = current_parallel_node ? "...start main and parallel frontier" : "...start main frontier only";
        LogMsg msg(st_msg);
        log_highlight_node(current_node, highlight::MAIN);
        if (current_parallel_node) {
            log_highlight_node(current_parallel_node, highlight::PARALLEL);
        }
    }
#endif


    // traverse the path from root address to address
    // inserting and stacking new proper nodes until
    // either we find a path-suffix that can be reused
    // or we traverse all the path
    while (index < addr.size())
    {

        // assuming address is contained in the current address
        Label label = addr[index];

        LinkType next_node_link_type;
        Node* next_node           = current_node->getChild(label, next_node_link_type);
        Node* next_parallel_node  = current_parallel_node ? current_parallel_node->getChild(label) : nullptr;

        // nextAddress index on current node
        // there is no next node
        if (!next_node)
        {
            // there is a parallel structure: update current node to
            // share the right child on the parallel structure
            if (next_parallel_node) {
                bool created   = true;
                current_node->setParentChildLink(label, next_parallel_node, SHARED, created);
                result_path.push_back(next_parallel_node); // append marker to the path

#ifdef LOG
                {
                    LogMsg msg("...sharing existing path on parallel branch");
                    log_update_child_link(current_node, label);
                }
#endif
                break;
            }
            else {

#ifdef LOG
                {
                    LogMsg msg("...creating new branch and advancing main frontier!");
#endif

                    attachNewProperChain(current_node);

#ifdef LOG
                    Node *aux = current_node;
                    for (size_t ii=index;ii<addr.size();ii++) {
                        aux = current_node->getChild(addr[ii]);
                        log_highlight_child_link(current_node, addr[ii], highlight::MAIN);
                        log_highlight_node(aux, highlight::MAIN);
                    }
                }
#endif

                break;
            }
        }
        else if (next_node_link_type == SHARED) {
            // assert(next_parallel_node);
            if (next_node == next_parallel_node) {
                result_path.push_back(next_node); // append end-marker

#ifdef LOG
                {
                    LogMsg msg("...main path already sharing with parallel path: done advancind frontier");
                }
#endif

                break;
            }
            else {

#if 1
                //
                // Here is where we need to check if there needs to be a copy
                // of the shared content or if we can switch the sharing to the
                // parallel branch (one for now).
                //
                bool share_from_parallel = false;

#ifdef LOG
                {
                    LogMsg msg("...checking possibility of switching current shared branch to parallel branch " + std::to_string(logging::getLog().getNodeName((uint64_t)next_node)));
                    std::vector<Node*> stack_upstream_path;
#endif

                    Node *aux = next_node;
                    while (true) {
                        if (aux->flag == Node::IN_PARALLEL_PATH) {
                            share_from_parallel = true; // found a proof that the parallel
                                                        // path contains the old shared branch:
                                                        // switch it!

                            {
                                LogMsg msg("...switching proof = node " + std::to_string(logging::getLog().getNodeName((uint64_t)aux)));
                            }

                            break;
                        }
                        else if (aux->flag == Node::IN_MAIN_PATH) {
                            {
                                LogMsg msg("...no switching. a copy is needed: proof = back to main path");
                            }
                            break;
                        }

                        if (aux->proper_parent != nullptr) {
#ifdef LOG
                            log_highlight_node(aux, highlight::UPSTREAM_CHECK);
                            log_highlight_child_link(aux->proper_parent,aux->label,highlight::UPSTREAM_CHECK);
                            stack_upstream_path.push_back(aux);
#endif
                            aux = aux->proper_parent;
                        }
                        else if (aux->owner != nullptr) {
#ifdef LOG
                            log_highlight_node(aux, highlight::UPSTREAM_CHECK);
                            log_highlight_content_link(aux->owner,highlight::UPSTREAM_CHECK);
                            stack_upstream_path.push_back(aux);
#endif
                            aux = aux->owner;
                        }
                        else {
                            break;
                        }
                    }


#ifdef LOG
                    // clear upstream path
                    while (!stack_upstream_path.empty()) {
                        Node* aux = stack_upstream_path.back();
                        stack_upstream_path.pop_back();

                        log_highlight_node(aux, highlight::CLEAR);
                        if (aux->proper_parent != nullptr) {
                            log_highlight_child_link(aux->proper_parent,aux->label,highlight::CLEAR);
                        }
                        else if (aux->owner != nullptr) {
                            log_highlight_content_link(aux->owner,highlight::CLEAR);
                        }
                    }
#endif


#ifdef LOG
                }
#endif


                if (share_from_parallel) {
                    next_node = next_parallel_node;
                    result_path.push_back(next_node); // append end-marker
                    bool created = false;
                    current_node->setParentChildLink(label, next_parallel_node, SHARED, created);
#ifdef LOG
                    {
                        LogMsg msg("...switching shared branches");
                        log_update_child_link(current_node,label);
                    }
#endif
                    break;
                }
                else {
                    // make lazy copy of next_node: its content is going to
                    // change and cannot interfere on the original version
                    next_node = next_node->shallowCopy();
                    bool created = false;
                    current_node->setParentChildLink(label, next_node, PROPER, created);
#ifdef LOG
                    {
                        LogMsg msg("...main path sharing branch not in parallel path: shallow copy, continue");
                        log_shallow_copy(next_node,current_dim,index+1);
                        log_update_child_link(current_node,addr[index]);
                    }
#endif
                }


#elif 0
                //
                // Wasteful strategy of always copying the shared branch
                // and inserting new content there.
                //

                // make lazy copy of next_node: its content is going to
                // change and cannot interfere on the original version
                next_node = next_node->shallowCopy();
                bool created = false;
                current_node->setParentChildLink(label, next_node, PROPER, created);

#ifdef LOG
                {

                    LogMsg msg("...main path sharing branch not in parallel path: shallow copy, continue");
                    log_shallow_copy(next_node,current_dim,index+1);
                    log_update_child_link(current_node,addr[index]);
                }

#endif

#elif 0
                // dumb and wrong strategy of switching always
                next_node = next_parallel_node;
                result_path.push_back(next_node); // append end-marker
                bool created = false;
                current_node->setParentChildLink(label, next_parallel_node, SHARED, created);
#ifdef LOG
                {
                    LogMsg msg("...switching shared child (we need to prove this!)");
                    log_update_child_link(current_node,label);
                }
#endif
                break;

#endif


            }
        }


#ifdef LOG
        {
            std::string st_msg = next_parallel_node ? "...advancing main and parallel frontier" : "...advancing main frontier only";
            LogMsg msg(st_msg);

            log_highlight_child_link(current_node, addr[index], highlight::MAIN);
            if (next_parallel_node) {
                log_highlight_child_link(current_parallel_node, addr[index], highlight::PARALLEL);
            }
#endif
            // update current_node, previous_node and stack
            current_node    = next_node;

            // update current_parallel_node
            current_parallel_node = next_parallel_node;

            // push paths
            result_path.push_back(current_node);
            current_node->setFlag(Node::IN_MAIN_PATH);
            if (current_parallel_node != nullptr) {
                parallel_path.push_back(current_parallel_node);
                current_parallel_node->setFlag(Node::IN_PARALLEL_PATH);
            }

#ifdef LOG
            log_highlight_node(current_node, highlight::MAIN);
            if (current_parallel_node) {
                // parallel_highlight_stack.push_back(current_parallel_node);
                log_highlight_node(current_parallel_node, highlight::PARALLEL);
            }
        }
#endif

        ++index;

        if (index == addr.size()) {
            result_path.push_back(nullptr); // base case
        }

    }

#ifdef LOG
//    {
//        if (parallel_highlight_stack.size() > 0) {
//            LogMsg msg("Cleaning parallel path");
//            while (!parallel_highlight_stack.empty()) {
//                log_highlight_node(parallel_highlight_stack.back(), highlight::CLEAR);
//                parallel_highlight_stack.pop_back();
//            }
//        }
//    }
#endif
}

#endif












//-----------------------------------------------------------------------------
// Node Impl.
//-----------------------------------------------------------------------------

Node *Node::shallowCopy() const {
    Node* copy = new Node(*this);
    for (auto &child: copy->children) {
        child.link_type = SHARED; // redefine all children as shared links
    }
    copy->content_link.link_type = SHARED;
    return copy;
}

Content* Node::getContent() const {
    return this->content_link.content;
}

void Node::setContent(Content *content, LinkType type)
{
    this->content_link.content = content;
    this->content_link.link_type = type;
    if (type == PROPER) {
        content->setOwner(this);
    }
}

bool Node::hasContent() const
{
    return content_link.content != nullptr;
}

Summary *Node::getContentAsSummary() const
{
    return reinterpret_cast<Summary*>(content_link.content);
}

Node *Node::getContentAsNode() const
{
    return reinterpret_cast<Node*>(content_link.content);
}

int Node::getNumChildren() const
{
    return children.size();
}

void Node::setParentChildLink(Label label, Node *node, LinkType link_type) {
    bool created;
    this->setParentChildLink(label,node,link_type,created);
}

void Node::setParentChildLink(Label label, Node *node, LinkType link_type, bool &created)
{
    created = false;
    auto comp = [](const ParentChildLink &e, Label lbl) {
        return e.label < lbl;
    };
    auto it = std::lower_bound(children.begin(), children.end(), label, comp);

    if (it != children.end() && it->label == label) {
        it->child = node;
        it->label = label;
        it->link_type = link_type;
    }
    else {
        children.insert(it, ParentChildLink(label, node, link_type));
        created = true;
    }

    if (link_type == PROPER) {
        node->setProperParent(this, label);
    }

}

Node* Node::getChild(Label label) const
{
    auto comp = [](const ParentChildLink &e, Label lbl) {
        return e.label < lbl;
    };
    auto it = std::lower_bound(children.begin(), children.end(), label, comp);

    if (it != children.end() && it->label == label) {
        return it->child;
    }
    else {
        return nullptr;
    }
}

Node* Node::getChild(Label label, LinkType &link_type) const
{
    auto comp = [](const ParentChildLink &e, Label lbl) {
        return e.label < lbl;
    };
    auto it = std::lower_bound(children.begin(), children.end(), label, comp);

    if (it != children.end() && it->label == label) {
        link_type = it->link_type;
        return it->child;
    }
    else {
        return nullptr;
    }
}

void Node::setProperParent(Node *parent, Label lbl)
{
    this->proper_parent = parent;
    this->label         = lbl;
}

void Node::setFlag(Node::Flag flag) const
{
    this->flag = flag;
}

LinkType Node::getContentType() const
{
    return content_link.link_type;
}



