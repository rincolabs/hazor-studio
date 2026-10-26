#include "Tree.hpp"

#include <algorithm>

namespace psd {

LayerNode LayerNode::layer(size_t index) { return LayerNode(Kind::Layer, index, std::nullopt, {}); }
LayerNode LayerNode::group(size_t index, std::optional<size_t> divider, std::vector<LayerNode> children) {
    return LayerNode(Kind::Group, index, divider, std::move(children));
}

size_t LayerNode::count() const {
    size_t n = 1;
    for (auto& c : m_children) n += c.count();
    return n;
}

std::ostream& operator<<(std::ostream& os, const LayerNode& n) {
    if (n.kind() == LayerNode::Kind::Layer) return os << "Layer{index=" << n.index() << "}";
    return os << "Group{index=" << n.index() << ", children=" << n.children().size() << "}";
}

std::vector<LayerNode> PsdFile::layerTree() const { return layerTreeMaxDepth(SIZE_MAX); }

std::vector<LayerNode> PsdFile::layerTreeMaxDepth(size_t maxDepth) const {
    const auto& ls = layers();
    // First pass: find the dividers a folder closes. A divider that is never
    // closed opens no group; its layers belong to the enclosing level.
    std::vector<bool> closed(ls.size(), false);
    std::vector<size_t> open;
    for (size_t i = 0; i < ls.size(); ++i) {
        SectionType st = ls[i].sectionType();
        if (st == SectionType::BoundingDivider) {
            open.push_back(i);
        } else if (st == SectionType::OpenFolder || st == SectionType::ClosedFolder) {
            if (!open.empty()) {
                size_t d = open.back();
                open.pop_back();
                if (d < closed.size()) closed[d] = true;
            }
        }
    }

    // Stack of (divider index, children); frame k holds the nodes at depth
    // k. Frames past maxDepth are tracked (to match dividers to folders) but
    // collect nothing.
    struct Frame {
        std::optional<size_t> divider;
        std::vector<LayerNode> children;
    };
    std::vector<Frame> stack;
    stack.push_back(Frame{std::nullopt, {}});
    // maxDepth == SIZE_MAX: maxDepth+1 would wrap; matches Rust's
    // saturating_add(1), which keeps it at usize::MAX (always allowed).
    auto push = [&](LayerNode node) {
        bool allowed = maxDepth == SIZE_MAX || stack.size() <= maxDepth + 1;
        if (allowed && !stack.empty()) stack.back().children.push_back(std::move(node));
    };
    for (size_t i = 0; i < ls.size(); ++i) {
        SectionType st = ls[i].sectionType();
        if (st == SectionType::BoundingDivider) {
            if (i < closed.size() && closed[i]) {
                stack.push_back(Frame{i, {}});
            }
        } else if (st == SectionType::OpenFolder || st == SectionType::ClosedFolder) {
            std::optional<size_t> divider;
            std::vector<LayerNode> children;
            if (stack.size() > 1) {
                divider = stack.back().divider;
                children = std::move(stack.back().children);
                stack.pop_back();
            }
            push(LayerNode::group(i, divider, std::move(children)));
        } else {
            push(LayerNode::layer(i));
        }
    }
    // Only closed dividers open frames, so none is left here; this just guards the invariant.
    while (stack.size() > 1) {
        auto children = std::move(stack.back().children);
        stack.pop_back();
        if (!stack.empty()) {
            auto& top = stack.back().children;
            top.insert(top.end(), std::make_move_iterator(children.begin()), std::make_move_iterator(children.end()));
        }
    }
    if (stack.empty()) return {};
    return std::move(stack.back().children);
}

} // namespace psd
