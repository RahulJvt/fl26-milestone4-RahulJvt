# M4 Design Note

## 1. Components and Threads

MainWindow will continue to own Workspace, manage the GUI, and coordinate application state on the main Qt thread. All widgets, signals, and user interactions will remain on that thread. I plan to introduce a background build worker using std::thread. The worker will receive a snapshot of the workspace documents and create a separate ProcessingCore. It will perform the corpus rebuild without modifying the GUI or the active ProcessingCore. Completed builds and failures will be reported back to the GUI thread, where MainWindow decides whether to accept the result.

## 2. Corpus States and Transitions

The application will track four states: Not Built, Building, Current, and Stale. Initially, the corpus is Not Built. Clicking Build Corpus starts a background build and enters Building. A successful build becomes Current only when it matches the latest workspace revision. Adding a document invalidates the current corpus and makes an active build outdated. Reset returns the application to Not Built. Failed builds return to a non-current state and display an error. Search, Term Inspector, and Context will remain unavailable unless the corpus is Current.

## 3. Event Sequences and Risks

**Scenario 1: Document added during a build**

1. The user starts building revision 4
2. The worker begins processing a snapshot of revision 4
3. The user adds another document, increasing the revision to 5
4. The original worker finishes
5. MainWindow rejects the revision 4 result because the workspace is now revision 5
6. The user can build the updated corpus without receiving outdated search results

**Scenario 2: Reset during a build**

1. A background build begins
2. The user clicks Reset while the worker is still running
3. The workspace is cleared and its revision increases
4. The GUI immediately returns to its initial state without waiting for the worker
5. If the worker finishes afterward, its result is discarded

Other risks include build failures, repeated Build clicks, and exiting during a build. I plan to allow one active build at a time, report failures on the GUI thread, and ensure that worker threads are joined during shutdown. The GUI will never wait for a build during ordinary user interactions.

## 4. Corpus Ownership

The worker will own its temporary ProcessingCore through a std::unique_ptr. After completing the build, ownership of the result will transfer to a completion object. MainWindow will accept that ProcessingCore only if its revision matches the current workspace. Accepting replaces the previous active corpus, which is then destroyed. Rejected or failed builds are destroyed without changing the active corpus. The worker's lifetime will be managed by an owning component, with no detached threads.

## 5. Rejected Alternative

I considered running ProcessingCore::rebuild() directly in MainWindow and periodically calling QApplication::processEvents() to keep the interface responsive. I rejected this because the build would still occupy the GUI thread, making event handling unpredictable and potentially allowing unsafe state changes during rebuilding. A separate worker with an isolated corpus provides clearer ownership and prevents partial results from becoming visible.