M4 Decision Record

Architecture and Threading

The application uses the M3 Qt GUI and M2 processing backend. Corpus construction runs on a separate std::thread to keep the GUI responsive. A std::future returns the completed result, and a QTimer checks for completion every 20 milliseconds. Worker threads never directly modify GUI widgets. The build thread is joined before the application closes.

Ownership and Result Validity

Each build uses a snapshot of the workspace. A revision counter increases whenever documents are added or the workspace is reset. When a build finishes, its revision is compared with the current revision. Outdated results are rejected. Successful results update the processing core on the GUI thread. Build failures are displayed in the application status area.

CR-1 Remote Add

The aiws_send program communicates with the application using a POSIX Unix domain stream socket. A separate server thread receives requests and forwards document additions to the GUI thread through a queued Qt invocation. The server waits for the GUI operation to finish before responding.

The protocol uses a four-byte big-endian path length followed by UTF-8 path bytes. Responses contain a status byte, a four-byte message length, and message text. Status 0 means accepted, while status 1 means rejected. Length framing supports filenames containing spaces, Unicode characters, and newlines, as well as requests delivered through multiple socket writes.

Testing and Fault Injection

Six original tests verify file handling, successful builds, GUI responsiveness, stale-result rejection, and build failure reporting. Four additional tests cover remote additions during builds, special filenames, split socket writes, and socket cleanup.

AIWS_FAULT_ACCEPT_STALE disables the revision check, while AIWS_FAULT_LOSE_FAILURE suppresses failure handling. Both fault-enabled builds compiled and produced the expected test failures. The normal build passed all tests.


Pair Programming Log

Session 1 – Remote Add Client

Date and Time:  11/5

Participants: Rahul and Chris
Driver: Rahul
Navigator: Chris

Task: Review and improve the aiws_send command line client.

Work Completed: The driver and navigator reviewed the socket connection process, pathname handling, response parsing, and error reporting. They checked that the client correctly handles valid and invalid document paths and returns the appropriate exit codes. The navigator provided feedback on error handling and code readability.

Testing and Results: The client was tested with valid and invalid document paths. The expected successful and unsuccessful responses were observed.

Session 2 – Remote Add Server and Testing

Date and Time: 11/5

Participants: Rahul and Chris
Driver: Chris
Navigator: Rahul Jadhav

Task: Review the Remote Add server and verify asynchronous behavior.

Work Completed: The driver and navigator reviewed the server's socket communication, GUI-thread interaction, and shutdown handling. They examined how adding a document during corpus construction invalidates outdated build results. They also reviewed tests for filenames containing spaces, Unicode characters, and newlines, along with split socket writes.

Testing and Results: The normal test suite passed all 10 tests. The two intentional fault configurations produced the expected test failures.

Summary

The two sessions demonstrated the driver and navigator workflow with roles reversed between sessions. The work focused on validating the Remote Add implementation, identifying potential issues, and reviewing automated test coverage.