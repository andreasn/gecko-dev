/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ActiveElementManager.h"
#include "mozilla/EventStates.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "inIDOMUtils.h"
#include "base/message_loop.h"
#include "base/task.h"
#include "mozilla/dom/Element.h"

#define AEM_LOG(...)
// #define AEM_LOG(...) printf_stderr("AEM: " __VA_ARGS__)

namespace mozilla {
namespace layers {

static int32_t sActivationDelayMs = 100;
static bool sActivationDelayMsSet = false;

ActiveElementManager::ActiveElementManager()
  : mDomUtils(services::GetInDOMUtils()),
    mCanBePan(false),
    mCanBePanSet(false),
    mSetActiveTask(nullptr)
{
  if (!sActivationDelayMsSet) {
    Preferences::AddIntVarCache(&sActivationDelayMs,
                                "ui.touch_activation.delay_ms",
                                sActivationDelayMs);
    sActivationDelayMsSet = true;
  }
}

ActiveElementManager::~ActiveElementManager() {}

void
ActiveElementManager::SetTargetElement(dom::EventTarget* aTarget)
{
  if (mTarget) {
    // Multiple fingers on screen (since HandleTouchEnd clears mTarget).
    AEM_LOG("Multiple fingers on-screen, clearing target element\n");
    CancelTask();
    ResetActive();
    ResetTouchBlockState();
    return;
  }

  mTarget = do_QueryInterface(aTarget);
  AEM_LOG("Setting target element to %p\n", mTarget.get());
  TriggerElementActivation();
}

void
ActiveElementManager::HandleTouchStart(bool aCanBePan)
{
  AEM_LOG("Touch start, aCanBePan: %d\n", aCanBePan);
  if (mCanBePanSet) {
    // Multiple fingers on screen (since HandleTouchEnd clears mCanBePanSet).
    AEM_LOG("Multiple fingers on-screen, clearing touch block state\n");
    CancelTask();
    ResetActive();
    ResetTouchBlockState();
    return;
  }

  mCanBePan = aCanBePan;
  mCanBePanSet = true;
  TriggerElementActivation();
}

void
ActiveElementManager::TriggerElementActivation()
{
  // Both HandleTouchStart() and SetTargetElement() call this. They can be
  // called in either order. One will set mCanBePanSet, and the other, mTarget.
  // We want to actually trigger the activation once both are set.
  if (!(mTarget && mCanBePanSet)) {
    return;
  }

  // If the touch cannot be a pan, make mTarget :active right away.
  // Otherwise, wait a bit to see if the user will pan or not.
  if (!mCanBePan) {
    SetActive(mTarget);
  } else {
    MOZ_ASSERT(mSetActiveTask == nullptr);
    mSetActiveTask = NewRunnableMethod(
        this, &ActiveElementManager::SetActiveTask, mTarget);
    MessageLoop::current()->PostDelayedTask(
        FROM_HERE, mSetActiveTask, sActivationDelayMs);
    AEM_LOG("Scheduling mSetActiveTask %p\n", mSetActiveTask);
  }
}

void
ActiveElementManager::HandlePanStart()
{
  AEM_LOG("Handle pan start\n");

  // The user started to pan, so we don't want mTarget to be :active.
  // Make it not :active, and clear any pending task to make it :active.
  CancelTask();
  ResetActive();
}

void
ActiveElementManager::HandleTouchEnd(bool aWasClick)
{
  AEM_LOG("Touch end, aWasClick: %d\n", aWasClick);

  // If the touch was a click, make mTarget :active right away.
  // nsEventStateManager will reset the active element when processing
  // the mouse-down event generated by the click.
  CancelTask();
  if (aWasClick) {
    SetActive(mTarget);
  } else {
    // We might reach here if mCanBePan was false on touch-start and
    // so we set the element active right away. Now it turns out the
    // action was not a click so we need to reset the active element.
    ResetActive();
  }

  ResetTouchBlockState();
}

void
ActiveElementManager::SetActive(dom::Element* aTarget)
{
  AEM_LOG("Setting active %p\n", aTarget);
  if (mDomUtils) {
    nsCOMPtr<nsIDOMElement> target = do_QueryInterface(aTarget);
    mDomUtils->SetContentState(target, NS_EVENT_STATE_ACTIVE.GetInternalValue());
  }
}

void
ActiveElementManager::ResetActive()
{
  AEM_LOG("Resetting active from %p\n", mTarget.get());

  // Clear the :active flag from mTarget by setting it on the document root.
  if (mTarget) {
    dom::Element* root = mTarget->OwnerDoc()->GetDocumentElement();
    if (root) {
      AEM_LOG("Found root %p, making active\n", root.get());
      SetActive(root);
    }
  }
}

void
ActiveElementManager::ResetTouchBlockState()
{
  mTarget = nullptr;
  mCanBePanSet = false;
}

void
ActiveElementManager::SetActiveTask(dom::Element* aTarget)
{
  AEM_LOG("mSetActiveTask %p running\n", mSetActiveTask);

  // This gets called from mSetActiveTask's Run() method. The message loop
  // deletes the task right after running it, so we need to null out
  // mSetActiveTask to make sure we're not left with a dangling pointer.
  mSetActiveTask = nullptr;
  SetActive(aTarget);
}

void
ActiveElementManager::CancelTask()
{
  AEM_LOG("Cancelling task %p\n", mSetActiveTask);

  if (mSetActiveTask) {
    mSetActiveTask->Cancel();
    mSetActiveTask = nullptr;
  }
}

}
}
