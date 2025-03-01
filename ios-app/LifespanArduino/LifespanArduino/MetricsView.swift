import SwiftUI
import HealthKit
import Charts

struct StepData: Identifiable {
    let id = UUID()
    let date: Date
    let total: Double
    let treadspanSteps: Double
    let lifespanFitSteps: Double
    let otherSteps: Double
    let label: String
}

struct MetricsView: View {
    // MARK: - HealthKit / Data
    private let healthStore = HKHealthStore()
    private let stepType = HKQuantityType.quantityType(forIdentifier: .stepCount)!
    
    // MARK: - UI State
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
    
    // MARK: - Permission View
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
    
    // MARK: - Main Data View
    private var dataView: some View {
        VStack(spacing: 16) {
            // If user tapped a bar, show details
            if let selected = selectedBar {
                selectedDataView(selected)
            }
            
            // Time-range buttons
            timeRangeSelector
            
            // Arrows + date range text
            dateNavigator
            
            // The Chart
            chartView
            
            // Treadmill total steps over last 365
            Text("Total Steps on Treadmill (last 365): \(totalLifespanSteps365)")
                .font(.subheadline)
            
            // Average steps & treadmill % if not “today (day)”
            if !isTodayView() {
                let (avg, treadmillPercent) = calculateDailyAverageAndTreadmillPercent()
                Text("Avg Steps/Day: \(Int(avg))  |  \(String(format: "%.1f", treadmillPercent))% on Treadmill")
                    .font(.footnote)
            }
        }
        .padding()
    }
    
    private func selectedDataView(_ data: StepData) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            if timeRange == .day {
                // For day view, show hour only
                Text(formatHour(data.date))
                    .font(.headline)
            } else {
                // Otherwise show full date
                Text(formatDate(data.date))
                    .font(.headline)
            }
            Text("Total Steps: \(Int(data.total))")
            Text("  - Treadmill Steps: \(Int(data.treadspanSteps + data.lifespanFitSteps))")
            Text("  - Other Steps: \(Int(data.otherSteps))")
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding()
        .background(Color.gray.opacity(0.1))
        .cornerRadius(10)
    }
    
    // MARK: - Time Range Selector
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
    
    // MARK: - Date Navigator
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
    
    // MARK: - The Chart
    private var chartView: some View {
        Chart {
            ForEach(stepData) { data in
                BarMark(
                    x: .value("Time", data.label),
                    y: .value("Steps", data.treadspanSteps)
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
        .frame(height: 250)
        // MARK: - Gestures remain unchanged
        .chartOverlay { _ in
            GeometryReader { geometry in
                Rectangle().fill(Color.clear).contentShape(Rectangle())
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
                Rectangle().fill(Color.clear).contentShape(Rectangle())
                    .gesture(
                        DragGesture(minimumDistance: 0)
                            .onEnded { value in
                                let localX = value.location.x - geometry.frame(in: .local).origin.x
                                if let index = getDataIndex(for: localX, width: geometry.size.width) {
                                    selectedBar = stepData[index]
                                }
                            }
                    )
            }
        }
        .chartOverlay { proxy in
            if let selectedData = selectedBar {
                if let idx = stepData.firstIndex(where: { $0.id == selectedData.id }) {
                    let stepWidth = proxy.plotAreaSize.width / CGFloat(stepData.count)
                    let xPos = stepWidth * (CGFloat(idx) + 0.5)
                    GeometryReader { geo in
                        Rectangle()
                            .fill(Color.secondary)
                            .frame(width: 1.0, height: geo.size.height)
                            .position(x: xPos, y: geo.size.height / 2)
                    }
                }
            }
        }
        // MARK: - Custom X-Axis
        .chartXAxis {
            if timeRange == .day {
                AxisMarks(values: stepData.map(\.label)) { axisValue in
                    if let labelValue = axisValue.as(String.self) {
                        // Only show the four labels we want
                        if labelValue == "12AM" ||
                           labelValue == "6AM"  ||
                           labelValue == "12PM" ||
                           labelValue == "6PM" {
                            AxisGridLine()
                            AxisTick()
                            AxisValueLabel {
                                Text(labelValue)
                                    .font(.caption2)   // Use a smaller font
                                    .fixedSize()       // Prevent the text from compressing
                            }
                        }
                    }
                }
            } else {
                AxisMarks()
            }
        }
    }
    
    // MARK: - Permissions
    private func requestHealthKitPermission() {
        let stepType = HKQuantityType.quantityType(forIdentifier: .stepCount)!
        let typesToRead: Set<HKObjectType> = [stepType]
        let typesToWrite: Set<HKSampleType> = []
        
        healthStore.requestAuthorization(toShare: typesToWrite, read: typesToRead) { success, error in
            DispatchQueue.main.async {
                hasPermission = success
                if success {
                    fetchHealthData()
                    fetchTotalLifespanSteps()
                }
            }
        }
    }
    
    // MARK: - Data Fetching
    private func fetchHealthData() {
        isLoading = true
        let (startDate, endDate, intervalComponents) = getDateRange()
        
        fetchStepsByInterval(start: startDate, end: endDate, components: intervalComponents) { totalSteps in
            fetchStepsByInterval(start: startDate, end: endDate, components: intervalComponents, sourceName: "TreadSpan") { treadspanSteps in
                fetchStepsByInterval(start: startDate, end: endDate, components: intervalComponents, sourceName: "LifeSpan") { fitSteps in
                    DispatchQueue.main.async {
                        let calendar = Calendar.current
                        if timeRange == .day {
                            // Build 24 hourly bins from midnight to midnight—even if zero steps
                            let startOfDay = calendar.startOfDay(for: currentDate)
                            var newData: [StepData] = []
                            for hour in 0..<24 {
                                let hourDate = calendar.date(byAdding: .hour, value: hour, to: startOfDay)!
                                // Use the queried data if available, or default to 0.
                                let totalCount = totalSteps[hourDate] ?? 0
                                let treadspanCount = treadspanSteps[hourDate] ?? 0
                                let fitCount = fitSteps[hourDate] ?? 0
                                let other = totalCount - (treadspanCount + fitCount)
                                newData.append(
                                    StepData(
                                        date: hourDate,
                                        total: totalCount,
                                        treadspanSteps: treadspanCount,
                                        lifespanFitSteps: fitCount,
                                        otherSteps: other,
                                        label: formatLabel(hourDate)
                                    )
                                )
                            }
                            self.stepData = newData
                        } else {
                            // For non-day views, use the queried dates directly.
                            self.stepData = totalSteps.map { date, totalCount in
                                let treadspanCount = treadspanSteps[date] ?? 0
                                let fitCount = fitSteps[date] ?? 0
                                let other = totalCount - (treadspanCount + fitCount)
                                return StepData(
                                    date: date,
                                    total: totalCount,
                                    treadspanSteps: treadspanCount,
                                    lifespanFitSteps: fitCount,
                                    otherSteps: other,
                                    label: formatLabel(date)
                                )
                            }.sorted { $0.date < $1.date }
                        }
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
        var treadspanTotal = 0
        var fitTotal = 0
        
        group.enter()
        fetchStepsByInterval(
            start: startDate,
            end: endDate,
            components: DateComponents(day: 1),
            sourceName: "TreadSpan"
        ) { steps in
            treadspanTotal = Int(steps.values.reduce(0, +))
            group.leave()
        }
        
        group.enter()
        fetchStepsByInterval(
            start: startDate,
            end: endDate,
            components: DateComponents(day: 1),
            sourceName: "LifeSpan"
        ) { steps in
            fitTotal = Int(steps.values.reduce(0, +))
            group.leave()
        }
        
        group.notify(queue: .main) {
            self.totalLifespanSteps365 = treadspanTotal + fitTotal
        }
    }
    
    // MARK: - HK Query
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
                
                let matchingSources = sources.filter { $0.name == sourceName }
                
                if !matchingSources.isEmpty {
                    let srcPredicate = HKQuery.predicateForObjects(from: matchingSources)
                    predicate = NSCompoundPredicate(andPredicateWithSubpredicates: [predicate, srcPredicate])
                } else {
                    completion([:]) // No source found
                    return
                }
                
                self.executeStatsQuery(
                    start: start, end: end,
                    intervalComponents: components,
                    predicate: predicate,
                    completion: completion
                )
            }
            healthStore.execute(sourceQuery)
        } else {
            executeStatsQuery(
                start: start, end: end,
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
            statsCollection.enumerateStatistics(from: start, to: end) { stats, _ in
                if let sum = stats.sumQuantity() {
                    stepMap[stats.startDate] = sum.doubleValue(for: .count())
                }
            }
            completion(stepMap)
        }
        
        healthStore.execute(query)
    }
    
    // MARK: - Date Range & Nav
    private func getDateRange() -> (Date, Date, DateComponents) {
        let calendar = Calendar.current
        let now = currentDate
        
        switch timeRange {
        case .day:
            let start = calendar.startOfDay(for: now)
            let endCandidate = calendar.date(byAdding: .day, value: 1, to: start)!
            return (start, min(endCandidate, Date()), DateComponents(hour: 1))
            
        case .week:
            let start = calendar.date(byAdding: .day, value: -6, to: calendar.startOfDay(for: now))!
            let endCandidate = calendar.date(byAdding: .day, value: 1, to: calendar.startOfDay(for: now))!
            return (start, min(endCandidate, Date()), DateComponents(day: 1))
            
        case .month:
            let firstOfThisMonth = calendar.date(from: calendar.dateComponents([.year, .month], from: now))!
            let endCandidate = calendar.date(byAdding: .month, value: 1, to: firstOfThisMonth)!
            return (firstOfThisMonth, min(endCandidate, Date()), DateComponents(day: 1))
            
        case .sixMonth:
            let firstOfThisMonth = calendar.date(from: calendar.dateComponents([.year, .month], from: now))!
            let start = calendar.date(byAdding: .month, value: -5, to: firstOfThisMonth)!
            let endCandidate = calendar.date(byAdding: .month, value: 1, to: firstOfThisMonth)!
            return (start, min(endCandidate, Date()), DateComponents(month: 1))
            
        case .year:
            let firstOfThisMonth = calendar.date(from: calendar.dateComponents([.year, .month], from: now))!
            let start = calendar.date(byAdding: .month, value: -11, to: firstOfThisMonth)!
            let endCandidate = calendar.date(byAdding: .month, value: 1, to: firstOfThisMonth)!
            return (start, min(endCandidate, Date()), DateComponents(month: 1))
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
    
    // MARK: - Formatting
    private func formatDate(_ date: Date) -> String {
        let f = DateFormatter()
        f.dateStyle = .medium
        return f.string(from: date)
    }
    
    private func formatHour(_ date: Date) -> String {
        let f = DateFormatter()
        f.dateFormat = "h a"
        return f.string(from: date)
    }
    
    private func formatLabel(_ date: Date) -> String {
        let f = DateFormatter()
        switch timeRange {
        case .day:
            f.dateFormat = "ha"  // e.g. "12AM", "1AM", etc.
        case .week:
            f.dateFormat = "EEE" // e.g. "Mon"
        case .month:
            f.dateFormat = "d"   // day of month
        case .sixMonth, .year:
            f.dateFormat = "MMM" // month name
        }
        return f.string(from: date)
    }
    
    private func getDateRangeText() -> String {
        let f = DateFormatter()
        switch timeRange {
        case .day:
            f.dateFormat = "MMMM d, yyyy"
            return f.string(from: currentDate)
        case .week:
            f.dateFormat = "MMM d"
            let end = Calendar.current.date(byAdding: .day, value: 6, to: currentDate)!
            return "\(f.string(from: currentDate)) - \(f.string(from: end))"
        case .month:
            f.dateFormat = "MMMM yyyy"
            return f.string(from: currentDate)
        case .sixMonth:
            f.dateFormat = "MMM yyyy"
            let start = Calendar.current.date(byAdding: .month, value: -5, to: currentDate)!
            return "\(f.string(from: start)) - \(f.string(from: currentDate))"
        case .year:
            f.dateFormat = "MMM yyyy"
            let start = Calendar.current.date(byAdding: .month, value: -11, to: currentDate)!
            return "\(f.string(from: start)) - \(f.string(from: currentDate))"
        }
    }
    
    // MARK: - Tap/Gesture Helpers
    private func getDataIndex(for xPosition: CGFloat, width: CGFloat) -> Int? {
        guard !stepData.isEmpty else { return nil }
        let stepWidth = width / CGFloat(stepData.count)
        let adjustedX = xPosition + (stepWidth / 2)
        var index = Int(adjustedX / stepWidth)
        index = max(0, min(index, stepData.count - 1))
        return index
    }
    
    // MARK: - Averages
    private func calculateDailyAverageAndTreadmillPercent() -> (Double, Double) {
        let totalSteps = stepData.reduce(0) { $0 + $1.total }
        let treadmillSteps = stepData.reduce(0) { $0 + ($1.treadspanSteps + $1.lifespanFitSteps) }
        let dayCount = stepData.count
        guard dayCount > 0 else { return (0, 0) }
        
        let average = totalSteps / Double(dayCount)
        let treadmillPercent = (treadmillSteps / totalSteps) * 100
        return (average, treadmillPercent)
    }
    
    private func isTodayView() -> Bool {
        timeRange == .day && Calendar.current.isDateInToday(currentDate)
    }
    
    private let hourAmPmFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "h a" // e.g. "1 PM"
        return f
    }()
}
