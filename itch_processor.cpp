// #include "itch_processor.h"
// #include "orderbook.h"
// #include <zlib.h>
// #include <variant>
// #include <stdexcept>
// #include <iostream>
// #include <immintrin.h>
// #include <numeric>
// #include <queue>
//
// void feed_handler(const gzFile file, SPSCQueue<QueuedOrder, 4096>& order_queue, std::atomic<bool>& running, Statistics& statistics) {
//     uint64_t total_messages{};
//     while (running.load(std::memory_order_relaxed)) {
//         if (total_messages % 10000 == 0) {
//             std::cout << "enqueued: " << statistics.enqueue << '\n';
//             std::cout << "dequeued: " << statistics.dequeue << '\n';
//         }
//         uint8_t length_bytes[2];
//         int n = gzread(file, length_bytes, 2);
//         if (n == 0) {
//             running.store(false, std::memory_order_release);
//             return;
//         }
//         if (n != 2) {
//             std::cerr << "Incomplete message" << '\n';
//             running.store(false, std::memory_order_release);
//             return;
//         }
//         uint16_t length = (static_cast<uint16_t>(length_bytes[0]) << 8) | static_cast<uint16_t>(length_bytes[1]);
//         if (length == 0 || length > 4096) {
//             throw std::runtime_error("Invalid ITCH message length");
//         }
//         std::vector<uint8_t> message(length);
//         n = gzread(file, message.data(), length);
//
//         if (n != length) {
//             throw std::runtime_error("Incomplete ITCH message body");
//         }
//         switch (message[0]) {
//             case 'A': {
//                 AddOrder add_order = decode_add_order(message);
//                 QueuedOrder queued_order{
//                     .order = add_order,
//                     .enqueue_time = Clock::now()
//                 };
//                 while (!(order_queue.push(queued_order))) {
//                     _mm_pause();
//                 };
//                 ++statistics.enqueue;
//                 break;
//             }
//             case 'F': {
//                 AddOrderWithMPID add_order_with_mpid = decode_add_order_with_MPID(message);
//                 QueuedOrder queued_order{
//                     .order = add_order_with_mpid,
//                     .enqueue_time = Clock::now()
//                 };
//                 while (!(order_queue.push(queued_order))) {
//                     _mm_pause();
//                 }
//                 ++statistics.enqueue;
//                 break;
//             }
//             case 'E' : {
//                 OrderExecuted order_executed = decode_order_executed(message);
//                 QueuedOrder queued_order{
//                     .order = order_executed,
//                     .enqueue_time = Clock::now()
//                 };
//                 while (!(order_queue.push(queued_order))) {
//                     _mm_pause();
//                 }
//                 ++statistics.enqueue;
//                 break;
//             }
//             case 'C': {
//                 OrderExecutedWithPrice order_executed_with_price  = decode_order_executed_with_price(message);
//                 QueuedOrder queued_order{
//                     .order = order_executed_with_price,
//                     .enqueue_time = Clock::now()
//                 };
//                 while (!(order_queue.push(queued_order))) {
//                     _mm_pause();
//                 }
//                 ++statistics.enqueue;
//                 break;
//             }
//             case 'X' : {
//                 OrderCancel order_cancel  = decode_order_cancel(message);
//                 QueuedOrder queued_order{
//                     .order = order_cancel,
//                     .enqueue_time = Clock::now()
//                 };
//                 while (!(order_queue.push(queued_order))) {
//                     _mm_pause();
//                 }
//                 ++statistics.enqueue;
//                 break;
//             }
//             case 'D' : {
//                 OrderDelete order_delete  = decode_order_delete(message);
//                 QueuedOrder queued_order{
//                     .order = order_delete,
//                     .enqueue_time = Clock::now()
//                 };
//                 while (!(order_queue.push(queued_order))) {
//                     _mm_pause();
//                 }
//                 ++statistics.enqueue;
//                 break;
//             }
//             case 'U' : {
//                 OrderReplace order_replace = decode_order_replace(message);
//                 QueuedOrder queued_order{
//                     .order = order_replace,
//                     .enqueue_time = Clock::now()
//                 };
//                 ++statistics.enqueue;
//                 while (!(order_queue.push(queued_order))) {
//                     _mm_pause();
//                 }
//             }
//         }
//         ++total_messages;
//     }
// }
//
//
//
// void order_book_processor(OrderBook& orderbook, SPSCQueue<QueuedOrder, 4096>& order_queue, const std::atomic<bool>& running, std::vector<uint64_t>& queue_latencies, Statistics& statistics)  {
//     while (running.load(std::memory_order_relaxed) || (!order_queue.empty())) {
//         // if (!queue_latencies.empty()) {
//         //     // auto [min_it, max_it] = std::minmax_element(queue_latencies.begin(), queue_latencies.end());
//         //     // uint64_t sum = std::accumulate(queue_latencies.begin(), queue_latencies.end(), static_cast<uint64_t>(0));
//         //     // double avg = static_cast<double>(sum) / queue_latencies.size();
//         //     // std::cout << "min latency: " << *min_it << '\n' << "max latency: " << *max_it << '\n' << "average latency: " << avg << '\n';
//         // }
//         if (statistics.total_messages % 10000 == 0) {
//             for (const auto& [stock_ticker, symbolbook] : orderbook.)
//             print_symbolbook(orderbook.symbolbooks.begin()) {
//
//             }
//         }
//         QueuedOrder out{};
//         if (order_queue.pop(out)) {
//             ++statistics.dequeue;
//             auto dequeue_time = Clock::now();
//             const auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(dequeue_time - out.enqueue_time).count();
//             queue_latencies.push_back(latency);
//             if (std::holds_alternative<AddOrder>(out.order)) {
//                 orderbook.add_order(std::get<AddOrder>(out.order));
//
//             } else if (std::holds_alternative<AddOrderWithMPID>(out.order)) {
//                 orderbook.add_order(std::get<AddOrderWithMPID>(out.order));
//             } else if (std::holds_alternative<OrderExecuted>(out.order)) {
//                 orderbook.execute_order(std::get<OrderExecuted>(out.order));
//             } else if (std::holds_alternative<OrderExecutedWithPrice>(out.order)) {
//                 orderbook.execute_order(std::get<OrderExecutedWithPrice>(out.order));
//             } else if (std::holds_alternative<OrderCancel>(out.order)) {
//                 orderbook.cancel_order(std::get<OrderCancel>(out.order));
//             } else if (std::holds_alternative<OrderDelete>(out.order)) {
//                 orderbook.delete_order(std::get<OrderDelete>(out.order));
//             } else if (std::holds_alternative<OrderReplace>(out.order)) {
//                 orderbook.replace_order(std::get<OrderReplace>(out.order));
//             }
//         }
//     }
// }
//
//
