#include <memory>
#include <algorithm>

#include "cppa/atom.hpp"
#include "cppa/exception.hpp"
#include "cppa/detail/actor_impl_util.hpp"
#include "cppa/detail/converted_thread_context.hpp"

namespace {

template<class List, typename Element>
bool unique_insert(List& lst, const Element& e)
{
    auto end = lst.end();
    auto i = std::find(lst.begin(), end, e);
    if (i == end)
    {
        lst.push_back(e);
        return true;
    }
    return false;
}

template<class List, typename Iterator, typename Element>
int erase_all(List& lst, Iterator from, Iterator end, const Element& e)
{
    auto i = std::find(from, end, e);
    if (i != end)
    {
        return 1 + erase_all(lst, lst.erase(i), end, e);
    }
    return 0;
}

template<class List, typename Element>
int erase_all(List& lst, const Element& e)
{
    return erase_all(lst, lst.begin(), lst.end(), e);
}

typedef std::lock_guard<std::mutex> guard_type;

} // namespace <anonymous>

namespace cppa { namespace detail {

converted_thread_context::converted_thread_context()
    : m_exit_reason(exit_reason::not_exited)
{
}

bool converted_thread_context::attach(attachable* ptr)
{
    return detail::do_attach<guard_type>(m_exit_reason,
                                         unique_attachable_ptr(ptr),
                                         m_attachables,
                                         m_mtx);
}

void converted_thread_context::detach(const attachable::token& what)
{
    detail::do_detach<guard_type>(what, m_attachables, m_mtx);
}

void converted_thread_context::cleanup(std::uint32_t reason)
{
    if (reason == exit_reason::not_exited) return;
    decltype(m_links) mlinks;
    decltype(m_attachables) mattachables;
    // lifetime scope of guard
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        m_exit_reason = reason;
        mlinks = std::move(m_links);
        mattachables = std::move(m_attachables);
        // make sure lists are definitely empty
        m_links.clear();
        m_attachables.clear();
    }
    actor_ptr mself = self();
    // send exit messages
    for (actor_ptr& aptr : mlinks)
    {
        aptr->enqueue(message(mself, aptr, atom(":Exit"), reason));
    }
    for (std::unique_ptr<attachable>& ptr : mattachables)
    {
        ptr->detach(reason);
    }
}

void converted_thread_context::quit(std::uint32_t reason)
{
    cleanup(reason);
    throw actor_exited(reason);
}

void converted_thread_context::link_to(intrusive_ptr<actor>& other)
{
    std::lock_guard<std::mutex> guard(m_mtx);
    if (other && !exited() && other->establish_backlink(this))
    {
        m_links.push_back(other);
        //m_links.insert(other);
    }
}

bool converted_thread_context::remove_backlink(const intrusive_ptr<actor>& other)
{
    if (other && other != this)
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        return erase_all(m_links, other) > 0;//m_links.erase(other) > 0;
    }
    return false;
}

bool converted_thread_context::establish_backlink(const intrusive_ptr<actor>& other)
{
    bool send_exit_message = false;
    bool result = false;
    if (other && other != this)
    {
        // lifetime scope of guard
        {
            std::lock_guard<std::mutex> guard(m_mtx);
            if (!exited())
            {
                result = unique_insert(m_links, other);
                //result = m_links.insert(other).second;
            }
            else
            {
                send_exit_message = true;
            }
        }
    }
    if (send_exit_message)
    {

    }
    return result;
}

void converted_thread_context::unlink_from(intrusive_ptr<actor>& other)
{
    std::lock_guard<std::mutex> guard(m_mtx);
    if (other && !exited() && other->remove_backlink(this))
    {
        erase_all(m_links, other);
        //m_links.erase(other);
    }
}

message_queue& converted_thread_context::mailbox()
{
    return m_mailbox;
}

} } // namespace cppa::detail
