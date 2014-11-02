require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))

class ReplicationTopologyTest < Verm::TestCase
  def port_for(instance_n)
    VERM_SPAWNER.port + 2 + instance_n
  end

  def post_something_to(verm)
    @location =
      post_file :path => '/foo',
                :file => 'binary_file.gz',
                :encoding => 'gzip',
                :expected_extension_suffix => 'gz',
                :type => 'application/octet-stream',
                :verm => verm
  end

  def get_options
    {
      :expected_content => File.read(fixture_file_path('binary_file.gz'), :mode => 'rb'),
      :expected_content_type => "application/octet-stream",
      :expected_content_encoding => 'gzip',
      :path => @location
    }
  end

  def test_propagates_around_open_loop
    spawners = (0..2).collect do |n|
      replicate_to = n.zero? ? nil : "#{VERM_SPAWNER.hostname}:#{port_for(n - 1)}"
      VermSpawner.new(VERM_SPAWNER.verm_binary, "#{VERM_SPAWNER.verm_data}_replica#{n}", :port => port_for(n), :replicate_to => replicate_to)
    end

    spawners.each(&:setup)

    before = spawners.collect {|spawner| get_statistics(:verm => spawner)}

    post_something_to(spawners[2])

    repeatedly_wait_until do
      get_statistics(:verm => spawners[1])[:replication_push_attempts] > 0
    end

    # all replicas should now have a copy
    spawners.each {|spawner| get get_options.merge(:verm => spawner)}

    # and all but the last should show successful pushes
    changes = spawners.collect.with_index {|spawner, index| calculate_statistics_change(before[index], get_statistics(:verm => spawner))}
    assert_equal([
      {:get_requests => 1, :put_requests => 1, :put_requests_new_file_stored => 1},
      {:get_requests => 1, :put_requests => 1, :put_requests_new_file_stored => 1, :replication_push_attempts => 1},
      {:get_requests => 1, :post_requests => 1, :post_requests_new_file_stored => 1, :replication_push_attempts => 1},
    ], changes)
  ensure
    spawners.each(&:teardown) if spawners
  end

  def test_propagates_around_closed_loop
    spawners = (0..2).collect do |n|
      replicate_to = n.zero? ? "#{VERM_SPAWNER.hostname}:#{port_for(2)}" : "#{VERM_SPAWNER.hostname}:#{port_for(n - 1)}"
      VermSpawner.new(VERM_SPAWNER.verm_binary, "#{VERM_SPAWNER.verm_data}_replica#{n}", :port => port_for(n), :replicate_to => replicate_to)
    end

    spawners.each(&:setup)

    before = spawners.collect {|spawner| get_statistics(:verm => spawner)}

    post_something_to(spawners[2])

    repeatedly_wait_until do
      get_statistics(:verm => spawners[0])[:replication_push_attempts] > 0
    end

    # all replicas should now have a copy
    spawners.each {|spawner| get get_options.merge(:verm => spawner)}

    # and all should have pushed, resulting in a new file on all but the original target
    changes = spawners.collect.with_index {|spawner, index| calculate_statistics_change(before[index], get_statistics(:verm => spawner))}
    assert_equal([
      {:get_requests => 1, :put_requests => 1, :put_requests_new_file_stored => 1, :replication_push_attempts => 1},
      {:get_requests => 1, :put_requests => 1, :put_requests_new_file_stored => 1, :replication_push_attempts => 1},
      {:get_requests => 1, :post_requests => 1, :post_requests_new_file_stored => 1, :put_requests => 1, :replication_push_attempts => 1},
    ], changes)
  ensure
    spawners.each(&:teardown) if spawners
  end

  def test_propagates_over_redundant_mesh
    spawners = (0..2).collect do |n|
      replicate_to = (0..2).collect {|rn| "#{VERM_SPAWNER.hostname}:#{port_for(rn)}" unless rn == n}.compact
      VermSpawner.new(VERM_SPAWNER.verm_binary, "#{VERM_SPAWNER.verm_data}_replica#{n}", :port => port_for(n), :replicate_to => replicate_to)
    end

    spawners.each(&:setup)

    before = spawners.collect {|spawner| get_statistics(:verm => spawner)}

    post_something_to(spawners[2])

    repeatedly_wait_until do
      get_statistics(:verm => spawners[0])[:replication_push_attempts] > 0
    end

    # all replicas should now have a copy
    spawners.each {|spawner| get get_options.merge(:verm => spawner)}

    # and all should have pushed, resulting in a new file on all but the original target
    changes = spawners.collect.with_index {|spawner, index| calculate_statistics_change(before[index], get_statistics(:verm => spawner))}
    assert_equal([
      {:get_requests => 1, :put_requests => 2, :put_requests_new_file_stored => 1, :replication_push_attempts => 2},
      {:get_requests => 1, :put_requests => 2, :put_requests_new_file_stored => 1, :replication_push_attempts => 2},
      {:get_requests => 1, :post_requests => 1, :post_requests_new_file_stored => 1, :put_requests => 2, :replication_push_attempts => 2},
    ], changes)
  ensure
    spawners.each(&:teardown) if spawners
  end
end
