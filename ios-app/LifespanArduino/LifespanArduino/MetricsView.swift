import SwiftUI
import HealthKit
import Charts

struct StepData: Identifiable {
    let id = UUID()
    let date: Date
    let total: Double
    let lifespanSteps: Double
    let otherSteps: Double
    let label: String
}

struct MetricsView: View {
    private let healthStore = HKHealthStore()
    @State private var timeRange: TimeRange = .day
    @State private var currentDate = Date()
    @State private var stepData: [StepData] = []
    @State private var selectedBar: StepData?
    @State private var isLoading = true
    @State private var hasPermission = false
    
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
        }
        .padding()
    }
    
    private func selectedDataView(_ data: StepData) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(formatDate(data.date))
                .font(.headline)
            
            Text("Total Steps: \(Int(data.total))")
            Text("Lifespan Steps: \(Int(data.lifespanSteps))")
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
                    y: .value("Steps", data.lifespanSteps)
                )
                .foregroundStyle(Color.blue)
                
                BarMark(
                    x: .value("Time", data.label),
                    y: .value("Steps", data.otherSteps)
                )
                .foregroundStyle(Color.green)
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
                }
            }
        }
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
    
    private func fetchHealthData() {
        isLoading = true
        let stepType = HKQuantityType.quantityType(forIdentifier: .stepCount)!
        
        let (startDate, endDate, intervalComponents) = getDateRange()
        
        // Query for total steps
        let predicate = HKQuery.predicateForSamples(withStart: startDate, end: endDate, options: .strictStartDate)
        
        let totalStepsQuery = HKStatisticsCollectionQuery(
            quantityType: stepType,
            quantitySamplePredicate: predicate,
            options: .cumulativeSum,
            anchorDate: startDate,
            intervalComponents: intervalComponents
        )
        
        // Query for Lifespan steps
        let sourcePredicate = HKQuery.predicateForObjects(
            withMetadataKey: HKMetadataKeyDeviceManufacturerName,
            allowedValues: ["LifespanArduino"]
        )
        let combinedPredicate = NSCompoundPredicate(andPredicateWithSubpredicates: [
            predicate,
            sourcePredicate
        ])
        
        let lifespanStepsQuery = HKStatisticsCollectionQuery(
            quantityType: stepType,
            quantitySamplePredicate: combinedPredicate,
            options: .cumulativeSum,
            anchorDate: startDate,
            intervalComponents: intervalComponents
        )
        
        var tempData: [Date: (total: Double, lifespan: Double)] = [:]
        
        let group = DispatchGroup()
        
        // Fetch total steps
        group.enter()
        totalStepsQuery.initialResultsHandler = { (query: HKStatisticsCollectionQuery, results: HKStatisticsCollection?, error: Error?) in
            if let results = results {
                results.enumerateStatistics(from: startDate, to: endDate) { statistics, stop in
                    if let sum = statistics.sumQuantity()?.doubleValue(for: HKUnit.count()) {
                        tempData[statistics.startDate] = (sum, tempData[statistics.startDate]?.lifespan ?? 0)
                    }
                }
            }
            group.leave()
        }
        
        // Fetch Lifespan steps
        group.enter()
        lifespanStepsQuery.initialResultsHandler = { (query: HKStatisticsCollectionQuery, results: HKStatisticsCollection?, error: Error?) in
            if let results = results {
                results.enumerateStatistics(from: startDate, to: endDate) { statistics, stop in
                    if let sum = statistics.sumQuantity()?.doubleValue(for: HKUnit.count()) {
                        let total = tempData[statistics.startDate]?.total ?? 0
                        tempData[statistics.startDate] = (total, sum)
                    }
                }
            }
            group.leave()
        }
        
        group.notify(queue: .main) {
            stepData = tempData.map { date, values in
                StepData(
                    date: date,
                    total: values.total,
                    lifespanSteps: values.lifespan,
                    otherSteps: values.total - values.lifespan,
                    label: formatLabel(date)
                )
            }.sorted { $0.date < $1.date }
            
            isLoading = false
        }
        
        healthStore.execute(totalStepsQuery)
        healthStore.execute(lifespanStepsQuery)
    }
}
