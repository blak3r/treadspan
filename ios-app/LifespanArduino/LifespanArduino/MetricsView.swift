import SwiftUI
import HealthKit
import Charts

struct StepData: Identifiable {
    let id = UUID()
    let date: Date
    let total: Double
    let lifespanArduinoSteps: Double
    let lifespanFitSteps: Double
    let otherSteps: Double
    let label: String
}

struct MetricsView: View {
    private let healthStore = HKHealthStore()
    private let stepType = HKQuantityType.quantityType(forIdentifier: .stepCount)!
    @State private var timeRange: TimeRange = .day
    @State private var currentDate = Date()
    @State private var stepData: [StepData] = []
    @State private var selectedBar: StepData?
    @State private var isLoading = true
    @State private var hasPermission = false
    @State private var totalLifespanSteps365: Int = 0
    
    enum TimeRange: String, CaseIterable {
        case day = "D"
        case week = "W"
        case month = "M"
        case sixMonth = "6M"
        case year = "Y"
    }
    
    var body: some View {
        VStack(spacing: 16) {
            if !hasPermission {
                permissionView
            } else if isLoading {
                ProgressView("Loading data...")
            } else {
                dataView
            }
        }
        .onAppear {
            requestHealthKitPermission()
        }
    }
    
    private var permissionView: some View {
        VStack(spacing: 16) {
            Text("Permission Required")
                .font(.headline)
            Button("Grant Permission") {
                requestHealthKitPermission()
            }
            .buttonStyle(.borderedProminent)
        }
    }
    
    private var dataView: some View {
        VStack(spacing: 16) {
            if let selected = selectedBar {
                selectedDataView(selected)
            }
            
            timeRangeSelector
            
            dateNavigator
            
            chartView
            
            Text("Total Steps on Treadmill (last 365): \(totalLifespanSteps365)")
                .font(.subheadline)
                .padding(.top, 8)
        }
        .padding()
    }
    
    private func selectedDataView(_ data: StepData) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(formatDate(data.date))
                .font(.headline)
            
            Text("Total Steps: \(Int(data.total))")
            Text("LifespanArduino Steps: \(Int(data.lifespanFitSteps + data.lifespanArduinoSteps))")
            Text("Lifespan Fit Steps: \(Int(data.lifespanFitSteps))")
            Text("Other Steps: \(Int(data.otherSteps))")
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding()
        .background(Color.gray.opacity(0.1))
        .cornerRadius(10)
    }
    
    private var timeRangeSelector: some View {
        HStack {
            ForEach(TimeRange.allCases, id: \.self) { range in
                Button(range.rawValue) {
                    withAnimation {
                        timeRange = range
                        fetchHealthData()
                        fetchTotalLifespanSteps()
                    }
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 6)
                .background(timeRange == range ? Color.blue : Color.gray.opacity(0.2))
                .foregroundColor(timeRange == range ? .white : .primary)
                .cornerRadius(8)
            }
        }
    }
    
    private var dateNavigator: some View {
        HStack {
            Button {
                navigateDate(forward: false)
            } label: {
                Image(systemName: "chevron.left")
            }
            
            Spacer()
            Text(getDateRangeText())
                .font(.headline)
            Spacer()
            
            Button {
                navigateDate(forward: true)
            } label: {
                Image(systemName: "chevron.right")
            }
        }
    }
    
    private var chartView: some View {
        Chart {
            ForEach(stepData) { data in
                BarMark(
                    x: .value("Time", data.label),
                    y: .value("Steps", data.lifespanArduinoSteps)
                )
                .foregroundStyle(Color.blue)
                
                BarMark(
                    x: .value("Time", data.label),
                    y: .value("Steps", data.lifespanFitSteps)
                )
                .foregroundStyle(Color.green)
                
                BarMark(
                    x: .value("Time", data.label),
                    y: .value("Steps", data.otherSteps)
                )
                .foregroundStyle(Color.gray.opacity(0.5))
            }
        }
        .frame(height: 200)
        .chartOverlay { proxy in
            GeometryReader { geometry in
                Rectangle().fill(.clear).contentShape(Rectangle())
                    .gesture(
                        DragGesture()
                            .onEnded { value in
                                let threshold: CGFloat = 50
                                if value.translation.width > threshold {
                                    navigateDate(forward: false)
                                } else if value.translation.width < -threshold {
                                    navigateDate(forward: true)
                                }
                            }
                    )
            }
        }
        .chartOverlay { proxy in
            GeometryReader { geometry in
                Rectangle().fill(.clear).contentShape(Rectangle())
                    .onTapGesture { location in
                        let relativeX = location.x - geometry.frame(in: .local).origin.x
                        if let index = getDataIndex(for: relativeX, width: geometry.size.width) {
                            selectedBar = stepData[index]
                        }
                    }
            }
        }
    }
    
    private func requestHealthKitPermission() {
        let stepType = HKQuantityType.quantityType(forIdentifier: .stepCount)!
        let typesToRead: Set = [stepType]
        
        healthStore.requestAuthorization(toShare: nil, read: typesToRead) { success, error in
            DispatchQueue.main.async {
                hasPermission = success
                if success {
                    fetchHealthData()
                    fetchTotalLifespanSteps()
                }
            }
        }
    }
    
    private func fetchHealthData() {
        isLoading = true
        let (startDate, endDate, intervalComponents) = getDateRange()
        
        // First fetch total steps
        fetchStepsByInterval(start: startDate, end: endDate, components: intervalComponents) { totalSteps in
            // Fetch LifespanArduino steps
            // Source name: LifespanArduino, bundleIdentifier: Robotion.LifespanArduino
            fetchStepsByInterval(start: startDate, end: endDate, components: intervalComponents, sourceName: "LifespanArduino") { arduinoSteps in
                // Then fetch Lifespan Fit steps
                // Source name: Lifespan, bundleIdentifier: com.app.lifespanfit
                fetchStepsByInterval(start: startDate, end: endDate, components: intervalComponents, sourceName: "Lifespan") { fitSteps in
                    DispatchQueue.main.async {
                        self.stepData = totalSteps.map { date, totalCount in
                            let arduinoCount = arduinoSteps[date] ?? 0
                            let fitCount = fitSteps[date] ?? 0
                            return StepData(
                                date: date,
                                total: totalCount,
                                lifespanArduinoSteps: arduinoCount,
                                lifespanFitSteps: fitCount,
                                otherSteps: totalCount - (arduinoCount + fitCount),
                                label: formatLabel(date)
                            )
                        }.sorted { $0.date < $1.date }
                        self.isLoading = false
                    }
                }
            }
        }
    }
    
    private func fetchTotalLifespanSteps() {
        let calendar = Calendar.current
        let endDate = Date()
        let startDate = calendar.date(byAdding: .day, value: -365, to: endDate)!
        
        let group = DispatchGroup()
        var arduinoTotal = 0
        var fitTotal = 0
        
        group.enter()
        fetchStepsByInterval(
            start: startDate,
            end: endDate,
            components: DateComponents(day: 1),
            sourceName: "LifespanArduino"
        ) { steps in
            arduinoTotal = Int(steps.values.reduce(0, +))
            group.leave()
        }
        
        group.enter()
        fetchStepsByInterval(
            start: startDate,
            end: endDate,
            components: DateComponents(day: 1),
            sourceName: "Lifespan"
        ) { steps in
            fitTotal = Int(steps.values.reduce(0, +))
            group.leave()
        }
        
        group.notify(queue: .main) {
            self.totalLifespanSteps365 = arduinoTotal + fitTotal
        }
    }
    
    private func fetchStepsByInterval(
        start: Date,
        end: Date,
        components: DateComponents,
        sourceName: String? = nil,
        completion: @escaping ([Date: Double]) -> Void
    ) {
        var predicate = HKQuery.predicateForSamples(withStart: start, end: end, options: .strictStartDate)
        
        if let sourceName = sourceName {
            let sourceQuery = HKSourceQuery(sampleType: stepType, samplePredicate: nil) { _, sourcesOrNil, error in
                guard let sources = sourcesOrNil, error == nil else {
                    completion([:])
                    return
                }
                
                // Debug: Print all sources
                print("Available sources:")
                sources.forEach { source in
                    print("Source name: \(source.name), bundleIdentifier: \(source.bundleIdentifier)")
                }
                
                if let targetSource = sources.first(where: { $0.name == sourceName }) {
                    let sourcePredicate = HKQuery.predicateForObjects(from: [targetSource])
                    predicate = NSCompoundPredicate(andPredicateWithSubpredicates: [predicate, sourcePredicate])
                } else {
                    print("Warning: Could not find source named \(sourceName)")
                    completion([:])
                    return
                }
                
                executeStatsQuery(
                    start: start,
                    end: end,
                    intervalComponents: components,
                    predicate: predicate,
                    completion: completion
                )
            }
            healthStore.execute(sourceQuery)
        } else {
            executeStatsQuery(
                start: start,
                end: end,
                intervalComponents: components,
                predicate: predicate,
                completion: completion
            )
        }
    }
    
    private func executeStatsQuery(
        start: Date,
        end: Date,
        intervalComponents: DateComponents,
        predicate: NSPredicate,
        completion: @escaping ([Date: Double]) -> Void
    ) {
        let query = HKStatisticsCollectionQuery(
            quantityType: stepType,
            quantitySamplePredicate: predicate,
            options: .cumulativeSum,
            anchorDate: start,
            intervalComponents: intervalComponents
        )
        
        query.initialResultsHandler = { _, results, error in
            guard let statsCollection = results, error == nil else {
                completion([:])
                return
            }
            
            var stepMap: [Date: Double] = [:]
            statsCollection.enumerateStatistics(from: start, to: end) { statistics, _ in
                if let sum = statistics.sumQuantity() {
                    stepMap[statistics.startDate] = sum.doubleValue(for: HKUnit.count())
                }
            }
            completion(stepMap)
        }
        
        healthStore.execute(query)
    }
    
    private func getDateRange() -> (start: Date, end: Date, interval: DateComponents) {
        let calendar = Calendar.current
        let now = currentDate
        
        switch timeRange {
        case .day:
            let start = calendar.startOfDay(for: now)
            let end = calendar.date(byAdding: .day, value: 1, to: start)!
            return (start, end, DateComponents(hour: 1))
            
        case .week:
            let start = calendar.date(byAdding: .day, value: -6, to: calendar.startOfDay(for: now))!
            let end = calendar.date(byAdding: .day, value: 1, to: calendar.startOfDay(for: now))!
            return (start, end, DateComponents(day: 1))
            
        case .month:
            let start = calendar.date(from: calendar.dateComponents([.year, .month], from: now))!
            let end = calendar.date(byAdding: .month, value: 1, to: start)!
            return (start, end, DateComponents(day: 1))
            
        case .sixMonth:
            let start = calendar.date(byAdding: .month, value: -5, to: calendar.date(from: calendar.dateComponents([.year, .month], from: now))!)!
            let end = calendar.date(byAdding: .month, value: 1, to: calendar.date(from: calendar.dateComponents([.year, .month], from: now))!)!
            return (start, end, DateComponents(month: 1))
            
        case .year:
            let start = calendar.date(byAdding: .month, value: -11, to: calendar.date(from: calendar.dateComponents([.year, .month], from: now))!)!
            let end = calendar.date(byAdding: .month, value: 1, to: calendar.date(from: calendar.dateComponents([.year, .month], from: now))!)!
            return (start, end, DateComponents(month: 1))
        }
    }
    
    private func navigateDate(forward: Bool) {
        let calendar = Calendar.current
        
        switch timeRange {
        case .day:
            currentDate = calendar.date(byAdding: .day, value: forward ? 1 : -1, to: currentDate)!
        case .week:
            currentDate = calendar.date(byAdding: .day, value: forward ? 7 : -7, to: currentDate)!
        case .month:
            currentDate = calendar.date(byAdding: .month, value: forward ? 1 : -1, to: currentDate)!
        case .sixMonth:
            currentDate = calendar.date(byAdding: .month, value: forward ? 6 : -6, to: currentDate)!
        case .year:
            currentDate = calendar.date(byAdding: .year, value: forward ? 1 : -1, to: currentDate)!
        }
        
        fetchHealthData()
        fetchTotalLifespanSteps()
    }
    
    private func formatDate(_ date: Date) -> String {
        let formatter = DateFormatter()
        formatter.dateStyle = .medium
        return formatter.string(from: date)
    }
    
    private func formatLabel(_ date: Date) -> String {
        let formatter = DateFormatter()
        
        switch timeRange {
        case .day:
            formatter.dateFormat = "HH:00"
        case .week:
            formatter.dateFormat = "EEE"
        case .month:
            formatter.dateFormat = "d"
        case .sixMonth, .year:
            formatter.dateFormat = "MMM"
        }
        
        return formatter.string(from: date)
    }
    
    private func getDateRangeText() -> String {
        let formatter = DateFormatter()
        
        switch timeRange {
        case .day:
            formatter.dateFormat = "MMMM d, yyyy"
            return formatter.string(from: currentDate)
        case .week:
            formatter.dateFormat = "MMM d"
            let endDate = Calendar.current.date(byAdding: .day, value: 6, to: currentDate)!
            return "\(formatter.string(from: currentDate)) - \(formatter.string(from: endDate))"
        case .month:
            formatter.dateFormat = "MMMM yyyy"
            return formatter.string(from: currentDate)
        case .sixMonth:
            formatter.dateFormat = "MMM yyyy"
            let startDate = Calendar.current.date(byAdding: .month, value: -5, to: currentDate)!
            return "\(formatter.string(from: startDate)) - \(formatter.string(from: currentDate))"
        case .year:
            formatter.dateFormat = "MMM yyyy"
            let startDate = Calendar.current.date(byAdding: .month, value: -11, to: currentDate)!
            return "\(formatter.string(from: startDate)) - \(formatter.string(from: currentDate))"
        }
    }
    
    private func getDataIndex(for xPosition: CGFloat, width: CGFloat) -> Int? {
        let stepWidth = width / CGFloat(stepData.count)
        let index = Int(xPosition / stepWidth)
        guard index >= 0 && index < stepData.count else { return nil }
        return index
    }
}
