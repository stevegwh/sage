//
// UI base types implementation. See UIBase.hpp.
//

#include "UIBase.hpp"

#include "../GameUiEngine.hpp" // for full TableCell (parent->GetRec)
#include "UIState.hpp"

#include <utility>

namespace sage
{
    void UIElement::OnHoverStart()
    {
    }

    void UIElement::OnHoverStop()
    {
    }

    void CellElement::OnClick()
    {
        onMouseClicked.Publish();
    }

    void CellElement::HoverUpdate()
    {
    }

    void CellElement::OnDragStart()
    {
        beingDragged = true;
    }

    void CellElement::OnDrop(CellElement* receiver)
    {
        beingDragged = false;
        if (receiver && receiver->canReceiveDragDrops)
        {
            receiver->ReceiveDrop(this);
        }
    }

    void CellElement::ReceiveDrop(CellElement* droppedElement)
    {
        if (!canReceiveDragDrops) return;
    }

    void CellElement::ChangeState(std::unique_ptr<UIState> newState)
    {
        if (newState == state || stateLocked) return;

        state->Exit();
        state = std::move(newState);
        state->Enter();
    }

    void CellElement::UpdateDimensions()
    {
        tex.width = parent->GetRec().width;
        tex.height = parent->GetRec().height;
    }

    CellElement::CellElement(
        GameUIEngine* _engine,
        TableCell* _parent,
        const VertAlignment _vertAlignment,
        const HoriAlignment _horiAlignment)
        : vertAlignment(_vertAlignment),
          horiAlignment(_horiAlignment),
          parent(_parent),
          engine(_engine),
          state(std::make_unique<IdleState>(this, engine))
    {
    }
} // namespace sage
