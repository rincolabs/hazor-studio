#pragma once

#include <QUuid>

// Stable, persistent identity for a LayerTreeNode.
//
// A LayerId:
//   - is saved in the document and stays the same when reopened;
//   - survives reorganization of the layer tree (it lives on the node);
//   - is DIFFERENT after a real layer duplication (assignNewIds());
//   - is PRESERVED by temporary render/export clones and undo snapshots;
//   - lets the animation model associate tracks with a node WITHOUT storing a
//     permanent pointer (tracks are keyed by LayerId, see AnimationModel).
//
// Never use the node's name, flat index, or memory address as identity.
// QUuid provides qHash(), so LayerId works directly as a QHash key.
using LayerId = QUuid;
