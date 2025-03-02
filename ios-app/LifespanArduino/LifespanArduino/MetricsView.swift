import SwiftUI
import HealthKit
import Charts

struct StepData: Identifiable {
    let id = UUID()
    
    /// The bucket range for these steps
    let startDate: Date
    let endDate: Date
    
    /// Aggregated step counts
    let total: Double
    let treadspanSteps: Double
    let lifespanFitSteps: Double
    let otherSteps: Double
    
    /// For your Chart’s X-axis label
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
    @State private var hasNavigatedMonth = false

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

            // Time-range buttons
            timeRangeSelector

            // If user tapped a bar, show details
            if let selected = selectedBar {
                selectedDataView(selected)
            }

            // Arrows + date range text
            dateNavigator

            // The Chart
            chartView

            // Average steps & treadmill % if not “today (day)”
            if !isTodayView() {
                let (avg, treadmillPercent) = calculateDailyAverageAndTreadmillPercent()
                Text("Avg Steps/Day: \(Int(avg))  |  \(String(format: "%.1f", treadmillPercent))% on Treadmill")
                    .font(.footnote)
            }
        }
        .padding()
    }

    // Helper to display e.g. "Feb 24 - Mar 2"
    private func formatDateRange(_ start: Date, to end: Date) -> String {
        let f = DateFormatter()
        f.dateFormat = "MMM d"
        let startStr = f.string(from: start)
        let endStr   = f.string(from: end)
        return "\(startStr) - \(endStr)"
    }
    
    private func selectedDataView(_ data: StepData) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            if timeRange == .day {
                // For day view, show hour only
                Text(formatHour(data.startDate))
                    .font(.headline)
            } else if timeRange == .sixMonth {
                // TODO remove one day from EndDate.
                // Otherwise show the full range (e.g. "Feb 24 - Mar 2")
                Text("\(formatDateRange(data.startDate, to: data.endDate))")
                    .font(.headline)
            } else {
                // Otherwise show full date
                Text(formatDate(data.startDate))
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
        // -- Left/Right Drag to navigate
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
        // -- Tap on a bar
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
        // -- Vertical marker when bar is selected
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
        // -- Custom X-Axis
        .chartXAxis {
            switch timeRange {
            case .day:
                AxisMarks(values: stepData.map(\.label)) { axisValue in
                    if let labelValue = axisValue.as(String.self) {
                        // Only show these four labels
                        if labelValue == "12AM" ||
                            labelValue == "6AM"  ||
                            labelValue == "12PM" ||
                            labelValue == "6PM" {
                            AxisGridLine()
                            AxisTick()
                            AxisValueLabel {
                                Text(labelValue)
                                    .font(.caption2)
                                    .fixedSize()
                            }
                        }
                    }
                }
            case .month, .sixMonth:
                // Filter labels to only include every 7th day
                let weeklyLabels = stepData.enumerated()
                    .filter { $0.offset % 7 == 0 }
                    .map { $0.element.label }

                AxisMarks(values: weeklyLabels) { axisValue in
                    if let label = axisValue.as(String.self) {
                        AxisGridLine()
                        AxisTick()
                        AxisValueLabel {
                            Text(label)
                                .font(.caption2)
                                .fixedSize()
                        }
                    }
                }
            default:
                // For W, 6M, Y, use default marks or all items
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
                }
            }
        }
    }

    // MARK: - Data Fetching
    private func fetchHealthData() {
        isLoading = true
        let (startDate, endDate, intervalComponents) = getDateRange()

        // We'll fetch total steps, TreadSpan steps, and LifeSpan steps in parallel and combine.
        fetchStepsByInterval(start: startDate, end: endDate, components: intervalComponents) { totalStepsDict in
            fetchStepsByInterval(start: startDate, end: endDate, components: intervalComponents, sourceName: "TreadSpan") { treadspanDict in
                fetchStepsByInterval(start: startDate, end: endDate, components: intervalComponents, sourceName: "LifeSpan") { fitDict in
                    DispatchQueue.main.async {
                        let calendar = Calendar.current

                        // For day view: build 24 hourly bins from midnight to midnight
                        if timeRange == .day {
                            let startOfDay = calendar.startOfDay(for: currentDate)
                            var newData: [StepData] = []
                            for hour in 0..<24 {
                                let hourDate = calendar.date(byAdding: .hour, value: hour, to: startOfDay)!
                                let totalCount = totalStepsDict[hourDate] ?? 0
                                let treadCount = treadspanDict[hourDate] ?? 0
                                let fitCount = fitDict[hourDate] ?? 0
                                let other = totalCount - (treadCount + fitCount)
                                newData.append(
                                    StepData(
                                        startDate: hourDate,
                                        endDate: endDate,
                                        total: totalCount,
                                        treadspanSteps: treadCount,
                                        lifespanFitSteps: fitCount,
                                        otherSteps: other,
                                        label: formatLabel(hourDate)
                                    )
                                )
                            }
                            self.stepData = newData
                        }
                        else {
                            // Example for non-day views
                            let allDates = Set(totalStepsDict.keys)
                                .union(treadspanDict.keys)
                                .union(fitDict.keys)

                            var combined: [StepData] = []
                            for startDate in allDates {
                                let total = totalStepsDict[startDate] ?? 0
                                let tread = treadspanDict[startDate] ?? 0
                                let fit   = fitDict[startDate] ?? 0
                                let other = total - (tread + fit)
                                
                                // For a given stats bucket, the "endDate" is typically the next bucket's start,
                                // or you can add the interval. But HKStatistics also gave us an 'endDate'
                                // in the enumerate closure.
                                // If you want exact endDate here, you could store that in a parallel dictionary
                                // or structure. A simpler approach is to compute it from the interval:
                                let endDate = Calendar.current.date(byAdding: intervalComponents, to: startDate) ?? startDate

                                combined.append(
                                  StepData(
                                    startDate: startDate,
                                    endDate: endDate,
                                    total: total,
                                    treadspanSteps: tread,
                                    lifespanFitSteps: fit,
                                    otherSteps: other,
                                    label: formatLabel(startDate)
                                  )
                                )
                            }
                            self.stepData = combined.sorted { $0.startDate < $1.startDate }
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
        var treadspanTotal = 0.0
        var fitTotal = 0.0

        group.enter()
        fetchStepsByInterval(
            start: startDate,
            end: endDate,
            components: DateComponents(day: 1),
            sourceName: "TreadSpan"
        ) { steps in
            treadspanTotal = steps.values.reduce(0, +)
            group.leave()
        }

        group.enter()
        fetchStepsByInterval(
            start: startDate,
            end: endDate,
            components: DateComponents(day: 1),
            sourceName: "LifeSpan"
        ) { steps in
            fitTotal = steps.values.reduce(0, +)
            group.leave()
        }

        group.notify(queue: .main) {
            // For daily intervals, we were storing total steps (not average)
            self.totalLifespanSteps365 = Int(treadspanTotal + fitTotal)
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
        // Base predicate: from start to end
        var predicate = HKQuery.predicateForSamples(withStart: start, end: end, options: .strictStartDate)

        // If we need to filter by source
        if let sourceName = sourceName {
            let sourceQuery = HKSourceQuery(sampleType: stepType, samplePredicate: nil) { _, sourcesOrNil, error in
                guard let sources = sourcesOrNil, error == nil else {
                    completion([:])
                    return
                }

                let matchingSources = sources.filter { $0.name == sourceName }

                // If no matching source, return empty
                if matchingSources.isEmpty {
                    completion([:])
                    return
                }

                // Combine the time-range predicate with a source predicate
                let srcPredicate = HKQuery.predicateForObjects(from: matchingSources)
                predicate = NSCompoundPredicate(andPredicateWithSubpredicates: [predicate, srcPredicate])

                self.executeStatsQuery(
                    start: start,
                    end: end,
                    intervalComponents: components,
                    predicate: predicate,
                    completion: completion
                )
            }
            healthStore.execute(sourceQuery)
        } else {
            // No sourceName, just run the stats query
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
            let calendar = Calendar.current
            
            statsCollection.enumerateStatistics(from: start, to: end) { stats, _ in
                if let sum = stats.sumQuantity() {
                    let totalSteps = sum.doubleValue(for: .count())
                    
                    // For partial intervals, count how many days are in this bucket
                    let dayCount = calendar.dateComponents([.day],
                                            from: stats.startDate, to: stats.endDate).day ?? 1
                    
                    var value = totalSteps
                    if timeRange == .sixMonth || timeRange == .year {
                        // Use an average steps/day for large spans
                        let safeDays = max(1, dayCount)
                        value = totalSteps / Double(safeDays)
                    }
                    
                    // Use the *startDate* of the bucket as our dictionary key
                    stepMap[stats.startDate] = value
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
            let calendar = Calendar.current
                if !hasNavigatedMonth && calendar.isDate(currentDate, inSameDayAs: Date()) {
                    // We are viewing the *current* month for the first time.
                    // Show last 30 days up to "today."
                    let start = calendar.date(byAdding: .day, value: -30, to: currentDate) ?? currentDate
                    let end = min(Date(), currentDate)  // just in case
                    return (start, end, DateComponents(day: 1))
                } else {
                    // Normal: show the full month.
                    let firstOfMonth = calendar.date(from: calendar.dateComponents([.year, .month],
                                                                from: currentDate))!
                    let nextMonth    = calendar.date(byAdding: .month, value: 1, to: firstOfMonth)!
                    // We’ll cap at ‘today’ if it’s the same month
                    return (firstOfMonth, min(nextMonth, Date()), DateComponents(day: 1))
                }

        case .sixMonth:
            // 6 months back from 'now'
            let start = calendar.date(byAdding: .month, value: -6, to: now) ?? now
            let end = min(now, Date())
            // Each bar = 7-day period
            return (start, end, DateComponents(day: 7))

        case .year:
            // 1 year back from 'now'
            let start = calendar.date(byAdding: .year, value: -1, to: now) ?? now
            let end = min(now, Date())
            // Each bar = 1-month period, but Y-axis is average steps/day
            return (start, end, DateComponents(month: 1))
        }
    }

    private func navigateDate(forward: Bool) {
        let calendar = Calendar.current
        
        switch timeRange {
        case .day:
            currentDate = calendar.date(byAdding: .day, value: forward ? 1 : -1, to: currentDate)!
            
        case .week:
            // shift the window by 7 days
            currentDate = calendar.date(byAdding: .day, value: forward ? 7 : -7, to: currentDate)!
            
        case .month:
            // If the user is pressing navigation, they've now "navigated"
            hasNavigatedMonth = true
            currentDate = calendar.date(byAdding: .month, value: forward ? 1 : -1, to: currentDate)!
            
        case .sixMonth:
            currentDate = calendar.date(byAdding: .month, value: forward ? 1 : -1, to: currentDate)!
            
        case .year:
            currentDate = calendar.date(byAdding: .year, value: forward ? 1 : -1, to: currentDate)!
        }
        
        fetchHealthData()
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
        case .sixMonth:
            // Each bar is 7 days; show e.g. "MMM d"
            f.dateFormat = "MMM d"
        case .year:
            // Each bar is a month; show e.g. "MMM"
            f.dateFormat = "MMM"
        }

        return f.string(from: date)
    }

    private func getDateRangeText() -> String {
        let calendar = Calendar.current
        let f = DateFormatter()

        switch timeRange {
        case .day:
            f.dateFormat = "MMMM d, yyyy"
            return f.string(from: currentDate)

        case .week:
            f.dateFormat = "MMM d"
            let end = calendar.date(byAdding: .day, value: 6, to: currentDate)!
            return "\(f.string(from: currentDate)) - \(f.string(from: end))"

        case .month:
            f.dateFormat = "MMMM yyyy"
            return f.string(from: currentDate)

        case .sixMonth:
            f.dateFormat = "MMM d, yyyy"
            let start = calendar.date(byAdding: .month, value: -6, to: currentDate)!
            return "\(f.string(from: start)) - \(f.string(from: currentDate))"

        case .year:
            f.dateFormat = "MMM d, yyyy"
            let start = calendar.date(byAdding: .year, value: -1, to: currentDate)!
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
