import SwiftUI
import HealthKit
import Charts

// Simple model to represent day-slice data for the chart.
struct DaySlice: Identifiable {
    let id = UUID()
    let date: Date
    let source: String          // "Lifespan Arduino" or "Other"
    let steps: Double
}

class MetricsViewModel: ObservableObject {
    
    // Published array of day slices for the stacked bar chart
    @Published var daySlices: [DaySlice] = []
    
    // Keep a reference to HKHealthStore
    private let healthStore = HKHealthStore()
    private let stepType = HKQuantityType.quantityType(forIdentifier: .stepCount)!
    
    // Call this once from the View’s .onAppear to ensure permissions.
    func requestAuthorizationIfNeeded() {
        // Request permission to read Step Count data
        healthStore.requestAuthorization(toShare: [], read: [stepType]) { success, error in
            if !success {
                print("HealthKit authorization failed: \(error?.localizedDescription ?? "")")
            }
        }
    }
    
    // Main entry point: fetch both total steps and device-specific steps, then build chart data
    func fetchData() {
        fetchDailyStepsForLast7Days { totalByDay in
            self.fetchDailyStepsForLast7Days(sourceName: "Lifespan Arduino") { deviceByDay in
                // Combine the results into daySlices for chart display
                DispatchQueue.main.async {
                    self.daySlices = self.buildDaySlices(totalByDay: totalByDay, deviceByDay: deviceByDay)
                }
            }
        }
    }
    
    // MARK: - Helper: Build the daySlices array for the stacked bar chart
    
    private func buildDaySlices(totalByDay: [Date: Double],
                                deviceByDay: [Date: Double]) -> [DaySlice] {
        
        // We'll create two "slices" per day:
        // 1) Lifespan Arduino steps
        // 2) All Other steps = total - Lifespan
        //
        // Then we'll flatten them into one array.
        
        var slices: [DaySlice] = []
        
        // Sort the days in ascending order
        let allDates = Set(totalByDay.keys).union(deviceByDay.keys).sorted()
        for date in allDates {
            let total = totalByDay[date] ?? 0
            let device = deviceByDay[date] ?? 0
            let other = max(total - device, 0)  // Just for safety
            
            let lifespanSlice = DaySlice(date: date, source: "Lifespan Arduino", steps: device)
            let otherSlice    = DaySlice(date: date, source: "All Other Sources", steps: other)
            
            slices.append(lifespanSlice)
            slices.append(otherSlice)
        }
        return slices
    }
    
    // MARK: - Fetch daily steps from HealthKit
    
    /// Fetch daily step counts (from all sources) for the last 7 days
    /// If sourceName is provided, filters to that source only
    private func fetchDailyStepsForLast7Days(sourceName: String? = nil,
                                             completion: @escaping ([Date: Double]) -> Void) {
        
        // Figure out our date interval for the last 7 days
        let calendar = Calendar.current
        let now = Date()
        guard let startDate = calendar.date(byAdding: .day, value: -7, to: now) else {
            completion([:])
            return
        }
        
        // Build predicate
        var predicate = HKQuery.predicateForSamples(withStart: startDate, end: now, options: .strictStartDate)
        
        // If we have a source name, fetch the HKSource object first, then run a stats collection.
        if let sourceName = sourceName {
            // We need to find the specific HKSource
            let sourceQuery = HKSourceQuery(sampleType: stepType, samplePredicate: nil) { [weak self] _, sourcesOrNil, error in
                guard let strongSelf = self, let sources = sourcesOrNil, error == nil else {
                    completion([:])
                    return
                }
                // Filter to the source with matching name
                if let targetSource = sources.first(where: { $0.name == sourceName }) {
                    let sourcePredicate = HKQuery.predicateForObjects(from: targetSource)
                    predicate = NSCompoundPredicate(andPredicateWithSubpredicates: [predicate, sourcePredicate])
                } else {
                    // If no source found, just return empty data
                    completion([:])
                    return
                }
                // Now do the collection query
                strongSelf.executeStatisticsCollectionQuery(predicate: predicate, completion: completion)
            }
            healthStore.execute(sourceQuery)
        } else {
            // No source filtering - all sources
            executeStatisticsCollectionQuery(predicate: predicate, completion: completion)
        }
    }
    
    /// Executes an HKStatisticsCollectionQuery to sum steps per day
    private func executeStatisticsCollectionQuery(predicate: NSPredicate,
                                                  completion: @escaping ([Date: Double]) -> Void) {
        
        // Set daily interval
        let calendar = Calendar.current
        var interval = DateComponents()
        interval.day = 1
        
        // Anchor at midnight
        let anchorComponents = calendar.dateComponents([.year, .month, .day],
                                                       from: Date())
        guard let anchorDate = calendar.date(from: anchorComponents) else {
            completion([:])
            return
        }
        
        let query = HKStatisticsCollectionQuery(quantityType: stepType,
                                                quantitySamplePredicate: predicate,
                                                options: .cumulativeSum,
                                                anchorDate: anchorDate,
                                                intervalComponents: interval)
        
        query.initialResultsHandler = { _, results, error in
            guard let statsCollection = results, error == nil else {
                completion([:])
                return
            }
            
            var stepByDay: [Date: Double] = [:]
            let endDate = Date()
            let startDate = Calendar.current.date(byAdding: .day, value: -7, to: endDate) ?? endDate
            
            // Enumerate the results from startDate to now
            statsCollection.enumerateStatistics(from: startDate, to: endDate) { statistics, _ in
                if let sum = statistics.sumQuantity() {
                    let date = statistics.startDate
                    let value = sum.doubleValue(for: .count())
                    stepByDay[calendar.startOfDay(for: date)] = value
                }
            }
            completion(stepByDay)
        }
        
        healthStore.execute(query)
    }
}

struct MetricsView: View {
    @StateObject private var viewModel = MetricsViewModel()
    
    var body: some View {
        VStack {
            if viewModel.daySlices.isEmpty {
                Text("Loading Steps…")
                    .font(.headline)
                    .padding()
            } else {
                // Create a stacked bar chart using SwiftUI Charts
                Chart(viewModel.daySlices) { slice in
                    BarMark(
                        x: .value("Date", slice.date, unit: .day),
                        y: .value("Steps", slice.steps)
                    )
                    // Color by the "source" property
                    .foregroundStyle(by: .value("Source", slice.source))
                    // .position() helps tell Charts to stack by "Source"
                    .position(by: .value("Source", slice.source))
                }
                // Format the X-axis as dates
                .chartXAxis {
                    AxisMarks(values: .stride(by: .day)) { value in
                        AxisGridLine()
                        AxisTick()
                        // For example: "Jan 31"
                        AxisValueLabel(format: .dateTime.day().month(.abbreviated))
                    }
                }
                .frame(height: 300)
                .padding()
            }
        }
        .navigationTitle("Metrics")
        .onAppear {
            // Ensure HealthKit authorization, then fetch the data
            viewModel.requestAuthorizationIfNeeded()
            viewModel.fetchData()
        }
    }
}
