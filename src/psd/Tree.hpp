#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <vector>

#include "File.hpp"

// Nested layer tree reconstruction from section dividers. See PROVENANCE.md
// for origin details.
//
// In the file, layers are stored bottom-to-top. A group appears as a
// bounding section divider (lsct type 3) below its children, followed by
// the children, followed by the group's own record (lsct type 1 open or
// 2 closed folder).

namespace psd {

// A node of the layer tree. Children are in file order (bottom-to-top).
class LayerNode {
public:
    enum class Kind { Layer, Group };

    static LayerNode layer(size_t index);
    static LayerNode group(size_t index, std::optional<size_t> divider, std::vector<LayerNode> children);

    Kind kind() const { return m_kind; }
    // Record index of this node.
    size_t index() const { return m_index; }
    // Index of the bounding section divider (groups only).
    std::optional<size_t> divider() const { return m_divider; }
    // Children (empty for layers).
    std::span<const LayerNode> children() const { return m_children; }
    // true for groups.
    bool isGroup() const { return m_kind == Kind::Group; }
    // Total number of nodes in this subtree (including self).
    size_t count() const;

    bool operator==(const LayerNode& other) const {
        return m_kind == other.m_kind && m_index == other.m_index && m_divider == other.m_divider && m_children == other.m_children;
    }
    bool operator!=(const LayerNode& other) const { return !(*this == other); }

private:
    LayerNode(Kind kind, size_t index, std::optional<size_t> divider, std::vector<LayerNode> children)
        : m_kind(kind), m_index(index), m_divider(divider), m_children(std::move(children)) {}
    Kind m_kind;
    size_t m_index;
    std::optional<size_t> m_divider;
    std::vector<LayerNode> m_children;
};
std::ostream& operator<<(std::ostream& os, const LayerNode& n);

} // namespace psd
